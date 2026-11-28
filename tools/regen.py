#!/usr/bin/env python3
"""Compatibility entry point for the shared project recipe driver."""
import os
from pathlib import Path
import runpy
import sys
ROOT = Path(__file__).resolve().parents[1]
engine = Path(os.environ.get('SNESRECOMP_ROOT', ROOT / 'snesrecomp')).resolve()
sys.argv[1:1] = ['--project-root', str(ROOT)]
runpy.run_path(str(engine / 'tools/regen_project.py'), run_name='__main__')
