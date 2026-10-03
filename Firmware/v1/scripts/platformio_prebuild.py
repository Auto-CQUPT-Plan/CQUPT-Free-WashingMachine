Import("env")

from pathlib import Path
import subprocess
import sys

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SCRIPT = PROJECT_DIR / "scripts" / "generate_web_assets.py"
WEB_SOURCES = [
    PROJECT_DIR / "web" / "index.html",
    PROJECT_DIR / "web" / "styles.css",
    PROJECT_DIR / "web" / "app.js",
    SCRIPT,
]
MAIN_OBJECT = "$BUILD_DIR/src/main.cpp.o"


def generate_web_assets(source, target, env):
    subprocess.check_call([sys.executable, str(SCRIPT)], cwd=str(PROJECT_DIR))


env.Depends(MAIN_OBJECT, [str(path) for path in WEB_SOURCES])
env.AddPreAction(MAIN_OBJECT, generate_web_assets)
