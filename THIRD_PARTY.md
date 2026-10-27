# Third-party software and data

SpinSpec's own source files are licensed under the GNU General Public License, version 3 or later, as stated in [LICENSE](LICENSE). That license does **not** replace or relicense third-party programs, libraries, or datasets.

The following components are used at build time, runtime, or in the optional web frontend. Their versions and applicable terms must be checked for the user's chosen installation.

| Component | Use in SpinSpec | License / upstream reference |
| --- | --- | --- |
| [GNU Scientific Library (GSL)](https://www.gnu.org/software/gsl/) | Symmetric eigensystem computation and GSL's CBLAS support in the C engine | GNU GPL; see [GSL licensing](https://www.gnu.org/software/gsl/doc/html/Introduction.html#Free-Software) |
| [Gnuplot](http://www.gnuplot.info/) | External plotting program executed by the C graphing module | [Gnuplot copyright/license](http://www.gnuplot.info/copyright.html), separate from SpinSpec |
| [Python](https://www.python.org/) | Running the optional web application | [Python Software Foundation license](https://docs.python.org/3/license.html) |
| [FastAPI](https://github.com/fastapi/fastapi) | Web application and API | MIT |
| [Pydantic](https://github.com/pydantic/pydantic) | Web-request data models and validation | MIT |
| [Uvicorn](https://github.com/encode/uvicorn) | ASGI server, when installed to serve the app | BSD-3-Clause |
| [GeoIP2 Python](https://github.com/maxmind/GeoIP2-python) | Optional GeoIP database lookup in the web server | Apache-2.0 |
| [Plotly.js](https://github.com/plotly/plotly.js) | Interactive plots loaded by `templates/dev_spectrum.html` from a third-party CDN | MIT |

Additional details:

- The C standard library, C math library (`libm`) and OpenMP runtime are supplied by the toolchain or operating system. Their terms depend on the selected distribution.
- The Plotly.js JavaScript bundle is loaded from an external CDN; it is **not** included in the repository.
- The `geoip2` package is distinct from the **MaxMind GeoLite2 database**. Redistributing or using MaxMind database files can involve separate conditions, including attribution; check the database terms before deploying or bundling data. No GeoLite2 database is included in this repository.
- Third-party executable packages, database contents and their license notices are **not** included merely by listing them here. If you redistribute those packages together with SpinSpec, comply with each package's license and preserve its notices.
- SpinSpec's image assets and documentation are not necessarily covered by third-party library licenses. Make sure you have the rights to redistribute any separately sourced material.

This file is descriptive and is not a substitute for the official notices distributed with individual dependencies.
