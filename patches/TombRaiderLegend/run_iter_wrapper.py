"""Wrapper that sets TRL_GAME_DIR before invoking run.py main."""
import os
import sys

os.environ['TRL_GAME_DIR'] = r'C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend'

REPO_ROOT = r'C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-'
os.chdir(REPO_ROOT)
sys.path.insert(0, REPO_ROOT)

# Forward CLI args by setting sys.argv and exec'ing run.py as __main__
script = os.path.join(REPO_ROOT, 'patches', 'TombRaiderLegend', 'run.py')
sys.argv = [script] + sys.argv[1:]

print(f'[wrapper] TRL_GAME_DIR = {os.environ["TRL_GAME_DIR"]}', flush=True)
print(f'[wrapper] argv = {sys.argv}', flush=True)

runpy_globals = {'__name__': '__main__', '__file__': script}
with open(script, 'r', encoding='utf-8') as f:
    code = compile(f.read(), script, 'exec')
exec(code, runpy_globals)
