import json
import re
import csv
import math
import tempfile
import hmac
import os
import shutil
import subprocess
import uuid
import logging
import threading
import time
import socket
import geoip2.database
from collections import defaultdict, deque
from fastapi import Request
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, field_validator

BASE_DIR = Path("/opt/nmr-web")
BIN_DIR = BASE_DIR / "bin"
JOBS_DIR = BASE_DIR / "jobs"
TEMPLATES_DIR = BASE_DIR / "templates"
STATIC_DIR = BASE_DIR / "static"
EXAMPLES_DIR = BASE_DIR / "examples"
ENGINE_BIN = BIN_DIR / "engine_matrix"
GRAPH_BIN = BIN_DIR / "matrix_graph"
GROUP_GRAPH_BIN = BIN_DIR / "group_graph"
REPLOT_BIN = BIN_DIR / "replot_spectrum"
INDEX_HTML = TEMPLATES_DIR / "index.html"
LOGS_DIR = BASE_DIR / "logs"
AUDIT_FILE = LOGS_DIR / "audit.jsonl"
ASN_DB = Path("/var/lib/GeoIP/GeoLite2-ASN.mmdb")
GEOIP_CACHE = {}
HOST_CACHE = {}

MAX_SPIN_COUNT = 8
JOB_TIMEOUT_SECONDS = 30
MIN_SECONDS_BETWEEN_REQUESTS = 5
MAX_REQUESTS_PER_MINUTE = 12

ADMIN_TOKEN = os.environ.get("ADMIN_TOKEN")

calc_lock = threading.Lock()
request_times = defaultdict(lambda: deque())
replot_request_times = defaultdict(lambda: deque())

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)

app = FastAPI(title="NMR Web Service")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")
app.mount("/jobs", StaticFiles(directory=str(JOBS_DIR)), name="jobs")


class SpectrumRequest(BaseModel):
    title: str = ""
    gammas: str = ""
    shifts: str = ""
    field: str = ""
    jcouplings: str = ""
    graph: str = ""
    heights: str = ""
    linewidths: str = ""
    x_left: str = ""
    x_right: str = ""
    observer_groups: str = ""
    example_name: str = ""
    example_modified: bool = False

    @field_validator(
        "title", "gammas", "shifts", "field",
        "jcouplings", "graph", "heights", "linewidths", "x_left", "x_right",
        "observer_groups"
    )
    @classmethod
    def strip_text(cls, value: str) -> str:
        return value.replace("\r", "").strip()


class ReplotRequest(SpectrumRequest):
    source_job_id: str


class JobResult(BaseModel):
    job_id: str
    set_filename: str
    image_url: Optional[str] = None
    spectrum_url: Optional[str] = None
    spectrum_csv_url: Optional[str] = None
    result_url: Optional[str] = None
    group_image_url: Optional[str] = None
    group_spectrum_url: Optional[str] = None
    message: str

class AuditRequest(BaseModel):
    event: str
    example_name: str = ""
    example_modified: bool = False
    title: str = ""
    spins: int = 0    


def ensure_layout() -> None:
    for path in [BASE_DIR, BIN_DIR, JOBS_DIR, TEMPLATES_DIR, STATIC_DIR, EXAMPLES_DIR, LOGS_DIR]:
        path.mkdir(parents=True, exist_ok=True)

    if not ENGINE_BIN.exists():
        raise RuntimeError(f"Binary not found: {ENGINE_BIN}")
    if not GRAPH_BIN.exists():
        raise RuntimeError(f"Binary not found: {GRAPH_BIN}")
    if not INDEX_HTML.exists():
        raise RuntimeError(f"Template not found: {INDEX_HTML}")


@app.on_event("startup")
def on_startup() -> None:
    ensure_layout()


@app.get("/", response_class=HTMLResponse)
def read_index(request: Request) -> HTMLResponse:
    ip = get_client_ip(request)
    audit_event("page_open", ip, {})
    return HTMLResponse(INDEX_HTML.read_text(encoding="utf-8"))

@app.get("/ru", response_class=HTMLResponse, include_in_schema=False)
@app.get("/ru/", response_class=HTMLResponse, include_in_schema=False)
def read_russian_page(request: Request) -> HTMLResponse:
    ip = get_client_ip(request)
    audit_event("page_open_ru", ip, {})
    return HTMLResponse((TEMPLATES_DIR / "ru.html").read_text(encoding="utf-8"))


def get_client_ip(request: Request) -> str:
    return (
        request.headers.get("CF-Connecting-IP")
        or request.headers.get("X-Forwarded-For", "").split(",")[0].strip()
        or (request.client.host if request.client else "unknown")
    )

@app.get("/tutorial", response_class=HTMLResponse)
def read_tutorial(request: Request) -> HTMLResponse:
    ip = get_client_ip(request)
    audit_event("tutorial_open", ip, {})
    return HTMLResponse((STATIC_DIR / "tutorial.html").read_text(encoding="utf-8"))

# SPINSPEC_DOCS_NAV_V11
@app.get("/about", response_class=HTMLResponse, include_in_schema=False)
@app.get("/about/", response_class=HTMLResponse, include_in_schema=False)
def about_en_page() -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "about_en.html").read_text(encoding="utf-8"))

@app.get("/tr", response_class=HTMLResponse, include_in_schema=False)
@app.get("/tr/", response_class=HTMLResponse, include_in_schema=False)
def about_tr_page() -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "about_tr.html").read_text(encoding="utf-8"))


@app.get("/dev-spectrum", response_class=HTMLResponse)
def read_dev_spectrum(request: Request) -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "dev_spectrum.html").read_text(encoding="utf-8"))

@app.get("/changes", response_class=HTMLResponse, include_in_schema=False)
@app.get("/changes/", response_class=HTMLResponse, include_in_schema=False)
def release_history() -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "changes.html").read_text(encoding="utf-8"))


@app.get("/health")
def healthcheck() -> dict:
    return {"status": "ok"}


@app.get("/api/examples")
def list_examples() -> dict:
    # SPINSPEC_CLASSIFIED_EXAMPLE_CATALOG
    import json as _catalog_json
    catalog_file = EXAMPLES_DIR / "catalog.json"
    known = {}
    groups = []
    if catalog_file.exists():
        catalog = _catalog_json.loads(catalog_file.read_text(encoding="utf-8"))
        for group in catalog.get("groups", []):
            group_name = str(group.get("category", "Other"))
            groups.append(group_name)
            for entry in group.get("items", []):
                known[entry["filename"]] = {
                    "filename": entry["filename"],
                    "label": entry["label"],
                    "category": group_name,
                    "model": entry.get("model", "reference"),
                }
    examples = [known[name] for name in known
                if (EXAMPLES_DIR / name).is_file()]
    for path in sorted(EXAMPLES_DIR.glob("*.set")):
        if path.name not in known:
            examples.append({
                "filename": path.name,
                "label": path.stem.replace("_", " ").title(),
                "category": "Other examples",
                "model": "reference",
            })
    return {"examples": examples}


@app.get("/api/examples/{filename}")
def get_example(filename: str) -> dict:
    if "/" in filename or "\\" in filename:
        raise HTTPException(status_code=400, detail="Invalid filename.")

    path = EXAMPLES_DIR / filename
    if not path.exists() or not path.is_file() or path.suffix != ".set":
        raise HTTPException(status_code=404, detail="Example not found.")

    return {
        "filename": path.name,
        "content": path.read_text(encoding="utf-8"),
    }

@app.post("/api/audit")
def frontend_audit(payload: AuditRequest, request: Request) -> dict:
    ip = get_client_ip(request)
    audit_event(payload.event, ip, payload.model_dump())
    return {"status": "ok"}

def sanitize_title(title: str) -> str:
    title = title.strip()
    if not title:
        return "# Untitled spectrum"
    if title.startswith("#"):
        return title
    return f"# {title}"

def audit_event(event_type: str, ip: str, data: dict) -> None:
    record = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "event": event_type,
        "ip": ip,
        "data": data,
    }
    with AUDIT_FILE.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")

def validate_payload(payload: SpectrumRequest) -> None:
    spin_count = infer_spin_count(payload)

    if spin_count == 0:
        raise HTTPException(status_code=400, detail="Unable to determine the number of spins.")

    if spin_count > MAX_SPIN_COUNT:
        raise HTTPException(
            status_code=400,
            detail=f"he task is too large: {spin_count} spins. The public limit is: {MAX_SPIN_COUNT}.",
        )

    if not payload.graph:
        raise HTTPException(status_code=400, detail="The 'Spin numbers' field must not be empty.")

    if not payload.linewidths:
        raise HTTPException(status_code=400, detail="The 'Graph widths' ield must not be empty.")

    bounds = []
    for value, label in ((payload.x_left, "left"), (payload.x_right, "right")):
        if not value:
            bounds.append(None)
            continue
        try:
            parsed = float(value)
        except ValueError:
            raise HTTPException(status_code=400, detail=f"Invalid {label} X boundary.")
        if not math.isfinite(parsed):
            raise HTTPException(status_code=400, detail=f"Invalid {label} X boundary.")
        bounds.append(parsed)
    if bounds[0] is not None and bounds[1] is not None and bounds[0] >= bounds[1]:
        raise HTTPException(status_code=400, detail="Left X boundary must be less than right X boundary.")

    if payload.observer_groups:
        items = payload.observer_groups.replace(",", " ").split()
        if len(items) != spin_count or any(
            not item.isdecimal() or int(item) > 9 for item in items
        ):
            raise HTTPException(
                status_code=400,
                detail=f"Expected exactly {spin_count} group IDs (0..9).",
            )
        if not GROUP_GRAPH_BIN.exists():
            raise HTTPException(
                status_code=503, detail="Group visualization is unavailable."
            )

def infer_spin_count(payload: SpectrumRequest) -> int:
    gammas = [x for x in payload.gammas.replace(",", " ").split() if x]
    shifts = [x for x in payload.shifts.replace(",", " ").split() if x]

    if gammas:
        return len(gammas)
    if shifts:
        return len(shifts)
    return 0

def normalize_spaces(s: str) -> str:
    return " ".join(s.replace(",", " ").split())

def normalize_jcouplings(s: str) -> str:
    lines = []
    for line in s.replace("\r", "").splitlines():
        line = " ".join(line.replace(",", " ").split())
        if line:
            lines.append(line)
    return "\n".join(lines)

# SPINSPEC_AUTO_GROUP_WINDOWS_V1
# The numerical engine still includes ALL spins. Only the Graph display windows
# change in group mode. The user's manual Graph settings remain unchanged in
# ordinary mode and in the form's Save set function.
def observer_group_graph_layout(payload: SpectrumRequest) -> tuple[str, str]:
    if not payload.observer_groups:
        return payload.graph, payload.heights

    group_ids = [int(v) for v in payload.observer_groups.replace(",", " ").split()]
    first_spin_for_group = {}
    for spin, group_id in enumerate(group_ids):
        first_spin_for_group.setdefault(group_id, spin)

    # Sorted group IDs; one representative spin per nonempty group.
    representatives = [first_spin_for_group[g] for g in sorted(first_spin_for_group)]
    original_spins = payload.graph.replace(",", " ").split()
    original_heights = payload.heights.replace(",", " ").split()
    fallback_height = original_heights[0] if original_heights else "1.0"
    height_by_spin = {}
    for pos, token in enumerate(original_spins):
        if pos < len(original_heights) and token.isdecimal():
            height_by_spin[int(token)] = original_heights[pos]

    return (
        " ".join(str(spin) for spin in representatives),
        " ".join(height_by_spin.get(spin, fallback_height) for spin in representatives),
    )


def build_set_content(payload: SpectrumRequest) -> str:
    group_graph, group_heights = observer_group_graph_layout(payload)
    records = [
        sanitize_title(payload.title),
        normalize_spaces(payload.gammas),
        normalize_spaces(payload.shifts),
        normalize_spaces(payload.field),
        normalize_jcouplings(payload.jcouplings),
        "",
        f"Graph {group_graph}",
        group_heights,
        payload.linewidths,
    ]
    # Optional extension after the legacy FWHM/half-width line.
    # Empty bounds are automatic, supplied values are offset-Hz limits.
    if payload.x_left or payload.x_right:
        records.append(f"XRange {payload.x_left or 'auto'} {payload.x_right or 'auto'}")
    records.append("")
    return "\n".join(records)


def physical_fingerprint(payload: SpectrumRequest) -> dict:
    """Only changes to these inputs require new Hamiltonian/observer data."""
    return {
        "gammas": normalize_spaces(payload.gammas),
        "shifts": normalize_spaces(payload.shifts),
        "field": normalize_spaces(payload.field),
        "jcouplings": normalize_jcouplings(payload.jcouplings),
        "observer_groups": normalize_spaces(payload.observer_groups),
    }


def create_job_dir() -> Path:
    job_id = uuid.uuid4().hex[:12]
    job_dir = JOBS_DIR / job_id
    job_dir.mkdir(parents=True, exist_ok=False)
    return job_dir


def write_set_file(job_dir: Path, payload: SpectrumRequest) -> Path:
    set_path = job_dir / "input.set"
    set_path.write_text(build_set_content(payload), encoding="utf-8")
    return set_path


def run_command(command: list[str], cwd: Path, env: Optional[dict] = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        capture_output=True,
        text=True,
        timeout=JOB_TIMEOUT_SECONDS,
        check=False,
    )


def make_graph_symlink(job_dir: Path) -> None:
    link_path = job_dir / "matrix_graph"
    if link_path.exists() or link_path.is_symlink():
        return
    link_path.symlink_to(GRAPH_BIN)


def make_group_graph_symlink(job_dir: Path) -> None:
    link_path = job_dir / "group_graph"
    if not link_path.exists() and not link_path.is_symlink():
        link_path.symlink_to(GROUP_GRAPH_BIN)


def observer_environment(payload: SpectrumRequest) -> Optional[dict]:
    if not payload.observer_groups:
        return None
    env = os.environ.copy()
    env["SPINSPEC_OBSERVER_GROUPS"] = ",".join(
        payload.observer_groups.replace(",", " ").split()
    )
    env["OMP_NUM_THREADS"] = "1"
    return env


# SPINSPEC_CSV_EXPORT_V1
# Export the legacy eigenstate-pair spectrum as self-documenting CSV.
# The legacy Sp_*.dat sixth column is a product-basis lookup, NOT a
# physical eigenstate-transition label, and is intentionally excluded.
def ensure_ordinary_csv(job_dir: Path) -> Optional[Path]:
    original = job_dir / "Sp_00000.dat"
    destination = job_dir / "Sp_transitions_00000.csv"
    if not original.is_file():
        return None
    if destination.is_file():
        return destination

    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="", suffix=".csv.tmp",
            prefix="SpinSpec_export_", dir=job_dir, delete=False
        ) as out_file:
            temporary = Path(out_file.name)
            writer = csv.writer(out_file)
            writer.writerow(("state_i", "state_j", "frequency_hz", "relative_strength"))
            with original.open("r", encoding="utf-8") as source:
                next(source)  # six-column engine header: n, x1, x2, i, j, I
                for line_number, line in enumerate(source, start=2):
                    if not line.strip():
                        continue
                    fields = line.split()
                    if len(fields) != 6:
                        raise ValueError(f"Invalid Sp data at line {line_number}: expected six columns")
                    frequency, strength = float(fields[1]), float(fields[2])
                    state_i, state_j = int(fields[3]), int(fields[4])
                    if (not math.isfinite(frequency) or not math.isfinite(strength)
                        or frequency < 0 or strength < -1e-10
                        or state_i < 1 or state_j <= state_i):
                        raise ValueError(f"Invalid transition values at line {line_number}")
                    writer.writerow((state_i, state_j, fields[1], fields[2]))
        temporary.replace(destination)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return destination


def collect_output(job_dir: Path) -> dict:
    image = job_dir / "data_00000.jpg"
    spectrum = job_dir / "Sp_00000.dat"
    group_image = job_dir / "group_spectrum_00000.jpg"
    invariant = job_dir / "Sp_invariant_00000.csv"

    result_candidates = sorted(job_dir.glob("Res_*_00000.dat"))
    result_file = result_candidates[0] if result_candidates else None

    return {
        "image": image if image.exists() else None,
        "group_image": group_image if group_image.exists() else None,
        "invariant": invariant if invariant.exists() else None,
        "spectrum": spectrum if spectrum.exists() else None,
        "result": result_file if result_file and result_file.exists() else None,
    }


def build_response(job_dir: Path, message: str) -> JobResult:
    ordinary_csv = ensure_ordinary_csv(job_dir)
    output = collect_output(job_dir)
    job_id = job_dir.name

    return JobResult(
        job_id=job_id,
        set_filename="input.set",
        image_url=f"/jobs/{job_id}/data_00000.jpg" if output["image"] else None,
        spectrum_url=f"/jobs/{job_id}/Sp_00000.dat" if output["spectrum"] else None,
        spectrum_csv_url=f"/jobs/{job_id}/Sp_transitions_00000.csv" if ordinary_csv else None,
        result_url=f"/jobs/{job_id}/{output['result'].name}" if output["result"] else None,
        group_image_url=(
            f"/jobs/{job_id}/group_spectrum_00000.jpg"
            if output["group_image"] else None
        ),
        group_spectrum_url=(
            f"/jobs/{job_id}/Sp_invariant_00000.csv"
            if output["invariant"] else None
        ),
        message=message,
    )


def failed_process_response(process: subprocess.CompletedProcess, action: str) -> HTTPException:
    stdout = process.stdout.strip()
    stderr = process.stderr.strip()
    details = {
        "action": action,
        "returncode": process.returncode,
        "stdout": stdout,
        "stderr": stderr,
    }
    return HTTPException(status_code=500, detail=json.dumps(details, ensure_ascii=False, indent=2))

def check_rate_limit(ip: str) -> None:
    now = time.time()
    q = request_times[ip]

    while q and now - q[0] > 60:
        q.popleft()

    if q and now - q[-1] < MIN_SECONDS_BETWEEN_REQUESTS:
        raise HTTPException(
            status_code=429,
            detail=f"Слишком частые запросы. Подождите {MIN_SECONDS_BETWEEN_REQUESTS} секунд.",
        )

    if len(q) >= MAX_REQUESTS_PER_MINUTE:
        raise HTTPException(
            status_code=429,
            detail="Слишком много запросов за минуту. Попробуйте позже.",
        )

    q.append(now)

def check_replot_rate_limit(ip: str) -> None:
    # Plot-only requests should not inherit the 5-second delay from full
    # diagonalizations, but still have a separate abuse-protection quota.
    now = time.time()
    recent = replot_request_times[ip]
    while recent and now - recent[0] > 60:
        recent.popleft()
    if (recent and now - recent[-1] < 0.75) or len(recent) >= 30:
        raise HTTPException(status_code=429, detail="Too many replot requests.")
    recent.append(now)


def geoip_lookup(ip: str) -> dict:
    if ip in GEOIP_CACHE:
        return GEOIP_CACHE[ip]

    info = {
        "country": "-",
        "city": "-",
        "org": "-",
    }

    try:
        if ASN_DB.exists():
            with geoip2.database.Reader(str(ASN_DB)) as reader:
                r = reader.asn(ip)
                info["org"] = r.autonomous_system_organization or "-"
    except Exception:
        pass

    GEOIP_CACHE[ip] = info
    return info

def classify_visitor(org: str, host: str, u: dict) -> str:
    text = f"{org} {host}".lower()

    if "googlebot.com" in text or "search.msn.com" in text:
        return "Search bot"

    if any(x in text for x in [
        "amazonaws", "googleusercontent", "digitalocean",
        "oracle", "leaseweb", "hosting", "hostroyale",
        "ovh", "vps", "server"
    ]):
        return "Cloud / scanner"

    if u["calculate"] > 0:
        return "User with calculations"

    return "Possible user"    

@app.post("/api/calculate", response_model=JobResult)
def calculate(payload: SpectrumRequest, request: Request) -> JobResult:
    ip = get_client_ip(request)
    spin_count = infer_spin_count(payload)

    check_rate_limit(ip)
    logging.info(f"CALCULATE ip={ip} spins={spin_count}")

    validate_payload(payload)

    if not calc_lock.acquire(blocking=False):
        raise HTTPException(
            status_code=429,
            detail="The server is currently busy with another calculation. Please try again shortly.",
        )

    job_dir = create_job_dir()

    try:
        set_path = write_set_file(job_dir, payload)
        make_graph_symlink(job_dir)
        if payload.observer_groups:
            make_group_graph_symlink(job_dir)

        process = run_command([str(ENGINE_BIN), set_path.name], cwd=job_dir,
                              env=observer_environment(payload))
        if process.returncode != 0:
            raise failed_process_response(process, "engine_matrix")

        response = build_response(job_dir, "Calculation completed.")
        if response.image_url is None:
            raise HTTPException(status_code=500, detail="Calculation completed without final image.")
        if payload.observer_groups and not response.group_image_url:
            raise HTTPException(
                status_code=500, detail="Group-resolved plot not generated."
            )
        # Store trusted physical provenance after the expensive calculation
        # succeeds, so subsequent display-only requests can use saved lines.
        (job_dir / "physical.json").write_text(
            json.dumps(physical_fingerprint(payload), ensure_ascii=False),
            encoding="utf-8",
        )
        audit_event("calculate", ip, {
            "spins": spin_count,
            "title": payload.title,
            "example_name": payload.example_name,
            "example_modified": payload.example_modified,
            "nuclei": infer_nuclei_from_gammas(payload.gammas),
            "field_mT": payload.field,
            "linewidths": payload.linewidths,
            "x_left": payload.x_left,
            "x_right": payload.x_right,
            "observer_groups": payload.observer_groups,
            "job_id": job_dir.name,
        })
            
        return response
     
    except subprocess.TimeoutExpired as exc:
        shutil.rmtree(job_dir, ignore_errors=True)
        raise HTTPException(status_code=504, detail=f"Calculation timeout exceeded: {exc.timeout} s.") from exc
    except Exception:
        if job_dir.exists() and not any(job_dir.iterdir()):
            shutil.rmtree(job_dir, ignore_errors=True)
        raise
    finally:
        calc_lock.release()


@app.post("/api/replot", response_model=JobResult)
def replot(payload: ReplotRequest, request: Request) -> JobResult:
    """Refresh display curves from saved transitions. Never invoke engine_matrix."""
    ip = get_client_ip(request)
    validate_payload(payload)
    check_replot_rate_limit(ip)

    if not re.fullmatch(r"[0-9a-f]{12}", payload.source_job_id):
        raise HTTPException(status_code=400, detail="Invalid source job ID.")

    source = JOBS_DIR / payload.source_job_id
    provenance = source / "physical.json"
    required = ["Sp_00000.dat", "Sp_dc_00000.dat",
                "Sp_tolerance_00000.dat"]
    if payload.observer_groups:
        required.append("Sp_invariant_00000.csv")
    if not provenance.is_file() or any(not (source / name).is_file()
                                       for name in required):
        raise HTTPException(
            status_code=409,
            detail="Reusable transition data are unavailable. Recalculate the model.",
        )
    try:
        original_physics = json.loads(provenance.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise HTTPException(status_code=409, detail="Invalid saved calculation metadata.")
    if original_physics != physical_fingerprint(payload):
        raise HTTPException(
            status_code=409,
            detail="Physical parameters have changed. Full calculation required.",
        )
    # Only single-field jobs are currently supported by this display cache.
    try:
        gammas = [float(v) for v in normalize_spaces(payload.gammas).split()]
        shifts = [float(v) for v in normalize_spaces(payload.shifts).split()]
        field = float(payload.field)
        fwhm, width = map(float, normalize_spaces(payload.linewidths).split())
        graph, _ = observer_group_graph_layout(payload)
        selected = [int(v) for v in graph.split()]
        if not (len(gammas) == len(shifts) == infer_spin_count(payload) and
                all(math.isfinite(v) for v in gammas + shifts) and
                math.isfinite(field) and math.isfinite(fwhm) and fwhm > 0 and
                math.isfinite(width) and width >= 0 and
                selected and len(set(selected)) == len(selected) and
                all(0 <= v < len(gammas) for v in selected)):
            raise ValueError("Invalid display parameters.")
        coupling_matrix = {}
        for line in normalize_jcouplings(payload.jcouplings).splitlines():
            a, b, j = line.split()
            a, b, j = int(a), int(b), float(j)
            if not math.isfinite(j) or not (0 <= a < len(gammas) and
                                            0 <= b < len(gammas)):
                raise ValueError("Invalid coupling.")
            coupling_matrix[(a, b)] = j
        total_abs_j = sum(abs(value) for (a, b), value
                          in coupling_matrix.items() if a < b)
    except (ValueError, TypeError):
        raise HTTPException(status_code=400, detail="Malformed replot parameters.")

    if not REPLOT_BIN.is_file():
        raise HTTPException(status_code=503, detail="Replot helper unavailable.")
    if not calc_lock.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="Calculation server busy.")

    job_dir = None
    try:
        job_dir = create_job_dir()
        for name in required:
            shutil.copy2(source / name, job_dir / name)
        write_set_file(job_dir, payload)
        make_graph_symlink(job_dir)
        if payload.observer_groups:
            make_group_graph_symlink(job_dir)

        # NaN in the helper config means automatic left/right limit.
        left = payload.x_left if payload.x_left else "nan"
        right = payload.x_right if payload.x_right else "nan"
        params = [
            f"{len(gammas)} {len(selected)} {field:.17g} "
            f"{fwhm:.17g} {width:.17g} {left} {right} {total_abs_j:.17g}"
        ]
        params.extend(f"{g:.17g} {sh:.17g}" for g, sh in zip(gammas, shifts))
        params.extend(str(spin) for spin in selected)
        (job_dir / "plot_params.dat").write_text(
            "\n".join(params) + "\n", encoding="utf-8",
        )
        process = run_command([str(REPLOT_BIN), "plot_params.dat"], cwd=job_dir)
        if process.returncode:
            raise failed_process_response(process, "replot_spectrum")
        process = run_command([str(GRAPH_BIN), "input.set", "0"], cwd=job_dir)
        if process.returncode:
            raise failed_process_response(process, "matrix_graph")

        if payload.observer_groups:
            spin_numbers = [int(v) for v in graph.split()]
            heights = observer_group_graph_layout(payload)[1].split()
            heights_spec = ",".join(
                f"{spin}:{height}" for spin, height in zip(spin_numbers, heights)
            )
            env = observer_environment(payload) or os.environ.copy()
            env["SPINSPEC_GRAPH_HEIGHTS"] = heights_spec
            env["SPINSPEC_X_LEFT"] = left if left != "nan" else "auto"
            env["SPINSPEC_X_RIGHT"] = right if right != "nan" else "auto"
            process = run_command(
                [str(GROUP_GRAPH_BIN), "0", f"{fwhm:.17g}", f"{width:.17g}"],
                cwd=job_dir, env=env,
            )
            if process.returncode:
                raise failed_process_response(process, "group_graph")

        response = build_response(job_dir, "Spectrum redrawn without diagonalization.")
        if not response.image_url or (payload.observer_groups and
                                      not response.group_image_url):
            raise HTTPException(status_code=500, detail="Replot image missing.")
        (job_dir / "physical.json").write_text(
            json.dumps(original_physics, ensure_ascii=False), encoding="utf-8",
        )
        audit_event("replot", ip, {
            "source_job_id": payload.source_job_id,
            "job_id": job_dir.name,
            "linewidths": payload.linewidths,
            "x_left": payload.x_left,
            "x_right": payload.x_right,
        })
        return response
    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=504, detail="Replot timeout exceeded.")
    finally:
        calc_lock.release()


@app.post("/api/update-graph", response_model=JobResult)
def update_graph(payload: SpectrumRequest) -> JobResult:
    validate_payload(payload)
    job_dir = create_job_dir()

    try:
        set_path = write_set_file(job_dir, payload)
        process = run_command([str(GRAPH_BIN), set_path.name, "0"], cwd=job_dir)
        if process.returncode != 0:
            raise failed_process_response(process, "matrix_graph")

        response = build_response(job_dir, "Graph updated.")
        if response.image_url is None:
            raise HTTPException(status_code=500, detail="Graph was not created.")
        return response

    except subprocess.TimeoutExpired as exc:
        shutil.rmtree(job_dir, ignore_errors=True)
        raise HTTPException(status_code=504, detail=f"Calculation timeout exceeded: {exc.timeout} s.") from exc
    except Exception:
        if job_dir.exists() and not any(job_dir.iterdir()):
            shutil.rmtree(job_dir, ignore_errors=True)
        raise


@app.get("/api/job/{job_id}", response_model=JobResult)
def get_job(job_id: str) -> JobResult:
    job_dir = JOBS_DIR / job_id
    if not job_dir.exists() or not job_dir.is_dir():
        raise HTTPException(status_code=404, detail="Job not found.")
    return build_response(job_dir, "Job state read.")

@app.get("/api/job/{job_id}/spectrum-lines")
def get_spectrum_lines(job_id: str) -> list[dict]:
    path = JOBS_DIR / job_id / "Sp_00000.dat"

    if not path.exists():
        raise HTTPException(status_code=404, detail="Spectrum file not found.")

    lines = []

    with path.open("r", encoding="utf-8") as f:
        next(f)
        for row in f:
            parts = row.split()
            if len(parts) >= 3:
                lines.append({
                    "freq": float(parts[1]),
                    "amp": float(parts[2])
                })

    return lines

@app.get("/api/job/{job_id}/set")
def get_set_file(job_id: str) -> FileResponse:
    set_path = JOBS_DIR / job_id / "input.set"
    if not set_path.exists():
        raise HTTPException(status_code=404, detail="SET file not found.")
    return FileResponse(path=set_path, filename=f"{job_id}.set", media_type="text/plain")
from pydantic import BaseModel

class FeedbackRequest(BaseModel):
    message: str

@app.post("/api/feedback")
def feedback(payload: FeedbackRequest, request: Request):
    ip = get_client_ip(request)

    audit_event("feedback", ip, {
        "message": payload.message
    })

    return {"status": "ok"}

def resolve_host(ip: str) -> str:
    if ip in HOST_CACHE:
        return HOST_CACHE[ip]

    try:
        host = socket.gethostbyaddr(ip)[0]
    except Exception:
        host = "-"

    HOST_CACHE[ip] = host
    return host

def gamma_to_nucleus(g: float) -> str:
    table = [
        ("H", 42.57747892),
        ("C", 10.7084),
        ("P", 17.235),
        ("F", 40.052),
        ("N", -4.316),
        ("Si", -8.465),
    ]

    for name, ref in table:
        if abs(g - ref) < 0.05:
            return name

    return f"custom({g:.4g})"

def infer_nuclei_from_gammas(gammas: str) -> str:
    result = []
    for x in gammas.replace(",", " ").split():
        try:
            g = float(x)
            result.append(gamma_to_nucleus(g))
        except Exception:
            result.append("?")
    return " ".join(result)        

@app.get("/admin/usage", response_class=HTMLResponse)
def admin_usage(token: str = "", mode: str = "all") -> HTMLResponse:
    if not ADMIN_TOKEN or not hmac.compare_digest(token, ADMIN_TOKEN):
        raise HTTPException(status_code=403, detail="Forbidden")

    if not AUDIT_FILE.exists():
        return HTMLResponse("<h1>No audit data yet</h1>")

    users = {}

    with AUDIT_FILE.open("r", encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except Exception:
                continue

            ip = rec.get("ip", "unknown")
            event = rec.get("event", "")
            ts = rec.get("ts", "")
            data = rec.get("data", {})

            u = users.setdefault(ip, {
                "ip": ip,
                "first": ts,
                "last": ts,
                "page_open": 0,
                "calculate": 0,
                "feedback": [],
                "examples": set(),
                "spins": [],
                "events": [],
            })

            u["last"] = ts

            if event == "calculate":
                field = data.get("field_mT", "-")
                lw = data.get("linewidths", "-")
                nuclei = data.get("nuclei", "-")
                job_id = data.get("job_id", "")

                if job_id:
                    u["events"].append(
                        f'{ts} — <a href="/dev-spectrum?job={job_id}" target="_blank">calculate</a>'
                        f' | B={field} mT | width={lw} | nuclei={nuclei}'
                    )
                else:
                    u["events"].append(
                        f"{ts} — calculate | B={field} mT | width={lw} | nuclei={nuclei}"
                    )
            else:
                u["events"].append(f"{ts} — {event}")

            if event == "page_open":
                u["page_open"] += 1

            elif event == "calculate":
                u["calculate"] += 1
                if data.get("example_name"):
                    u["examples"].add(data.get("example_name"))
                if data.get("spins"):
                    u["spins"].append(str(data.get("spins")))

            elif event == "feedback":
                msg = data.get("message", "")
                if msg:
                    u["feedback"].append(f"{ts}: {msg}")

    rows = []
    for ip, u in sorted(users.items(), key=lambda x: x[1]["last"], reverse=True):

        if mode == "calculations" and u["calculate"] == 0:
            continue

        host = resolve_host(ip)
        geo = geoip_lookup(ip)
        org = geo["org"]
        visitor_type = classify_visitor(org, host, u)

        is_bot_like = (
            u["calculate"] == 0 and (
                "Cloudflare" in org
                or "Amazon" in org
                or "Google" in org
                or "DigitalOcean" in org
                or "Oracle" in org
                or "Leaseweb" in org
                or "Hosting" in org
                or "Techoff" in org
            )
        )

        if mode == "real" and is_bot_like:
            continue
        examples = ", ".join(sorted(u["examples"])) if u["examples"] else "-"
        spins = ", ".join(u["spins"]) if u["spins"] else "-"
        feedback = "<br>".join(u["feedback"]) if u["feedback"] else "-"
        events = "<br>".join(u["events"][-8:])

        rows.append(f"""
        <tr>
          <td>{u["ip"]}</td>
          <td>{visitor_type}</td>
          <td>{org}</td>
          <td>{u["first"]}</td>
          <td>{u["last"]}</td>
          <td>{u["page_open"]}</td>
          <td>{u["calculate"]}</td>
          <td>{examples}</td>
          <td>{spins}</td>
          <td>{feedback}</td>
          <td>{events}</td>
        </tr>
        """)

    html = f"""
    <!DOCTYPE html>
    <html>
    <head>
      <link rel="icon" type="image/x-icon" href="/static/favicon.ico">
      <meta charset="utf-8">
      <title>SpinSpec usage</title>
      <style>
        body {{ font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }}
        table {{ border-collapse: collapse; width: 100%; background: white; }}
        th, td {{ border: 1px solid #ccc; padding: 8px; vertical-align: top; font-size: 13px; }}
        th {{ background: #e5e7eb; }}
        h1 {{ margin-top: 0; }}

        th:nth-child(9), td:nth-child(9) {{
          width: 100px;
          white-space: normal;
        }}

        th:nth-child(11), td:nth-child(11) {{
          width: 450px;
          white-space: normal;
          font-family: monospace;
          font-size: 12px;
        }}

      </style>
    </head>
    <body>
      <h1>SpinSpec usage</h1>
      <p>
        <a href="/admin/usage?token={token}&mode=all">All</a> |
        <a href="/admin/usage?token={token}&mode=real">Hide bots</a> |
        <a href="/admin/usage?token={token}&mode=calculations">Only calculations</a>
        </p>
        <p>Current mode: <b>{mode}</b></p>
      <table>
        <tr>
        <th>IP</th>
        <th>Visitor Type</th>
        <th>Organization</th>
        <th>First seen</th>
        <th>Last seen</th>
        <th>Page opens</th>
        <th>Calculations</th>
        <th>Examples</th>
        <th>Spins</th>
        <th>Feedback</th>
        <th>Last events</th>
        </tr>
        {''.join(rows)}
      </table>
    </body>
    </html>
    """

    return HTMLResponse(html)
@app.get("/robots.txt")
def robots_txt():
    return FileResponse(STATIC_DIR / "robots.txt", media_type="text/plain")

@app.get("/sitemap.xml")
def sitemap_xml():
    return FileResponse(STATIC_DIR / "sitemap.xml", media_type="application/xml")

@app.get("/yandex_03783da657843245.html", include_in_schema=False)
def yandex_verification():
    return FileResponse(
        STATIC_DIR / "yandex_03783da657843245.html",
        media_type="text/html"
    )

if __name__ == "__main__":
    import uvicorn

    uvicorn.run("app:app", host="127.0.0.1", port=8000, reload=False)
