import sys
from pathlib import Path

# Only reason this exists is so that imports work, no matter if scripts are executed as standalone
# or through setup.py - no complex setup, you run the script and it does its job

TOOLS_DIR = Path(__file__).resolve().parent

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
