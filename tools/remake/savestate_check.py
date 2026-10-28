#!/usr/bin/env python3
"""Savestate e descarga (fase 5; build-debug, TCP 4370).

1. Partida do 2 jogadores (BAG2_MATCH, padrão HEAT x COMET), salva o estado no slot `slot` e carrega de volta 5 s
   depois: o remake tem de continuar (páginas achadas de novo, nenhum quadro original).
2. Sai da partida (QUIT): os dois personagens descarregam no menu. Carrega o estado de novo: a partida volta com os
   assets descarregados e o primeiro desenho pede a carga (`load_draw`); mede quanto tempo o original aparece até o
   remake voltar e tira uma captura no modo 3 depois.
Uso: python tools/remake/savestate_check.py [slot]
Saída: tools/recomp/logs/remake/jogo/savestate.txt e savestate_*.png
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402

KEYS = ('frames_heat', 'frames_outros', 'match_fail', 'load_tim', 'load_draw', 'evict', 'evict_done', 'pages_scan')


def main():
    slot = int(sys.argv[1]) if len(sys.argv) > 1 else 7
    out = open(os.path.join(LOGS, 'savestate.txt'), 'a', encoding='utf-8')

    def log(*a):
        line = ' '.join(str(x) for x in a)
        print(line, flush=True)
        out.write(line + '\n'); out.flush()

    s = Session(label='savestate')
    s.start()

    def cnt():
        return {c['name'][10:]: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']
                if c['name'].startswith('bag2_heat.') and c['name'][10:] in KEYS}

    def chars():
        head = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '').split('||')[0]
        return re.sub(r'\s+', ' ', head)[:300]

    def delta(a, b):
        return {k: b.get(k, 0) - a.get(k, 0) for k in KEYS if b.get(k, 0) - a.get(k, 0)}

    try:
        log(f'=== sessão {time.strftime("%Y-%m-%d %H:%M")}, slot {slot}')
        s.enter_match()
        time.sleep(6)
        log('salvar:', s.cmd({'cmd': 'savestate', 'slot': slot, 'op': 'save'}))
        time.sleep(5)
        c0 = cnt()
        log('carregar (na partida):', s.cmd({'cmd': 'savestate', 'slot': slot, 'op': 'load'}))
        time.sleep(4)
        c1 = cnt()
        log('   4 s depois:', delta(c0, c1), '|', chars())
        s.quit_match()
        time.sleep(6)
        log('menu depois do QUIT:', chars())
        c2 = cnt()
        t0 = time.time()
        log('carregar (no menu):', s.cmd({'cmd': 'savestate', 'slot': slot, 'op': 'load'}))
        # até os dois voltarem a ser desenhados pelo remake
        back = None
        while time.time() - t0 < 15:
            d = delta(c2, cnt())
            if d.get('frames_heat', 0) > 5 and d.get('frames_outros', 0) > 5:
                back = time.time() - t0
                break
            time.sleep(0.1)
        log(f'   remake de volta em {back:.1f} s' if back else '   remake não voltou em 15 s', '|', delta(c2, cnt()))
        time.sleep(3)
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})
        time.sleep(0.3)
        p = os.path.join(LOGS, 'savestate_volta.png')
        s.shot(p)
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1'})
        log('   captura', p, '|', chars(), '|', s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
