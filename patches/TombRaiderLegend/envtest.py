import os, sys
sys.path.insert(0, r'C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-')
print('ENV:', os.environ.get('TRL_GAME_DIR', 'NOT_SET'))
from config import GAME_DIR
print('GAME_DIR:', GAME_DIR)
