import json
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
    example_name: str = ""
    example_modified: bool = False

    @field_validator(
        "title", "gammas", "shifts", "field",
        "jcouplings", "graph", "heights", "linewidths"
    )
    @classmethod
    def strip_text(cls, value: str) -> str:
        return value.replace("\r", "").strip()


class JobResult(BaseModel):
    job_id: str
    set_filename: str
    image_url: Optional[str] = None
    spectrum_url: Optional[str] = None
    result_url: Optional[str] = None
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

@app.get("/dev-spectrum", response_class=HTMLResponse)
def read_dev_spectrum(request: Request) -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "dev_spectrum.html").read_text(encoding="utf-8"))

@app.get("/health")
def healthcheck() -> dict:
    return {"status": "ok"}


@app.get("/api/examples")
def list_examples() -> dict:
    examples = []
    for path in sorted(EXAMPLES_DIR.glob("*.set")):
        examples.append(
            {
                "filename": path.name,
                "label": path.stem.replace("_", " ").title(),
            }
        )
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

def build_set_content(payload: SpectrumRequest) -> str:
    return "\n".join(
        [
            sanitize_title(payload.title),
            normalize_spaces(payload.gammas),
            normalize_spaces(payload.shifts),
            normalize_spaces(payload.field),
            normalize_jcouplings(payload.jcouplings),
            "",
            f"Graph {payload.graph}",
            payload.heights,
            payload.linewidths,
            "",
        ]
    )


def create_job_dir() -> Path:
    job_id = uuid.uuid4().hex[:12]
    job_dir = JOBS_DIR / job_id
    job_dir.mkdir(parents=True, exist_ok=False)
    return job_dir


def write_set_file(job_dir: Path, payload: SpectrumRequest) -> Path:
    set_path = job_dir / "input.set"
    set_path.write_text(build_set_content(payload), encoding="utf-8")
    return set_path


def run_command(command: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        cwd=str(cwd),
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


def collect_output(job_dir: Path) -> dict:
    image = job_dir / "data_00000.jpg"
    spectrum = job_dir / "Sp_00000.dat"

    result_candidates = sorted(job_dir.glob("Res_*_00000.dat"))
    result_file = result_candidates[0] if result_candidates else None

    return {
        "image": image if image.exists() else None,
        "spectrum": spectrum if spectrum.exists() else None,
        "result": result_file if result_file and result_file.exists() else None,
    }


def build_response(job_dir: Path, message: str) -> JobResult:
    output = collect_output(job_dir)
    job_id = job_dir.name

    return JobResult(
        job_id=job_id,
        set_filename="input.set",
        image_url=f"/jobs/{job_id}/data_00000.jpg" if output["image"] else None,
        spectrum_url=f"/jobs/{job_id}/Sp_00000.dat" if output["spectrum"] else None,
        result_url=f"/jobs/{job_id}/{output['result'].name}" if output["result"] else None,
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

        process = run_command([str(ENGINE_BIN), set_path.name], cwd=job_dir)
        if process.returncode != 0:
            raise failed_process_response(process, "engine_matrix")

        response = build_response(job_dir, "Calculation completed.")
        if response.image_url is None:
            raise HTTPException(status_code=500, detail="Calculation completed without final image.")
        audit_event("calculate", ip, {
            "spins": spin_count,
            "title": payload.title,
            "example_name": payload.example_name,
            "example_modified": payload.example_modified,
            "nuclei": infer_nuclei_from_gammas(payload.gammas),
            "field_mT": payload.field,
            "linewidths": payload.linewidths,
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

if __name__ == "__main__":
    import uvicorn

    uvicorn.run("app:app", host="127.0.0.1", port=8000, reload=False)
