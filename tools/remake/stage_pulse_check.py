#!/usr/bin/env python3
"""Confere o pulso do palco contra a música (fase 3): não a olho, pelo tempo do jogo e pelo som.

Numa partida, amostra o `stage_post` (tick da música do jogo, fase da batida e envelope do pulso que vai no quadro)
o mais rápido possível e grava o áudio do SPU no mesmo trecho. Relata:
  1. fase: em que quadro o envelope tem o pico em relação à virada da batida do tick (esperado: + atraso do som);
  2. som: correlação cruzada entre o envelope (por quadro) e os transientes do áudio (caixa/chimbal, 150 Hz..16 kHz);
     o pico deve cair perto de 0 quadros (pulso junto com o som).
Uso: python tools/remake/stage_pulse_check.py [segundos]
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palco')


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 25.0
    s = Session(label='palco_pulso')
    s.start()
    scratch = os.path.join(OUT, 'marca.wav').replace('\\', '/')
    try:
        s.enter_match()
        pts, rows = [], []
        def mark():
            f = s.cmd({'cmd': 'frame'})['frame']
            r = s.cmd({'cmd': 'audio_wav', 'path': scratch, 'count': 1})
            pts.append((f, int(r['tap_total'])))
        mark()
        t0 = time.time()
        while time.time() - t0 < secs:
            f0 = s.cmd({'cmd': 'frame'})['frame']
            r = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': ''}).get('reply', '')
            f1 = s.cmd({'cmd': 'frame'})['frame']
            m = re.search(r'tick=(\d+) rate=([\d.]+) beat=([-\d.]+) bar=([-\d.]+) env=([\d.]+)', r)
            if m:
                rows.append(((f0 + f1) / 2.0, int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(5))))
        mark()
        a0 = pts[0][1]
        r = s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'pulso.wav').replace('\\', '/'), 'start': str(a0),
                   'count': str(pts[-1][1] - a0)}, 60)
        with open(os.path.join(OUT, 'pulso.json'), 'w', encoding='utf-8') as fp:
            json.dump({'pts': pts, 'rows': rows, 'audio': r}, fp)
        print('amostras', len(rows), s.fps())
    finally:
        s.stop()
    analyse()


def analyse():
    J = json.load(open(os.path.join(OUT, 'pulso.json'), encoding='utf-8'))
    R = np.array(J['rows']); pts = np.array(J['pts'], float)
    fr, tick, rate, env = R[:, 0], R[:, 1], R[:, 2], R[:, 4]
    a, b = (pts[-1, 1] - pts[0, 1]) / (pts[-1, 0] - pts[0, 0]), pts[0, 1]
    # 1. pico do envelope x virada da batida do tick
    F = np.arange(int(fr[0]) + 1, int(fr[-1]))
    tk = np.interp(F, fr, tick)
    envf = np.interp(F, fr, env)
    ph = (tk % 256.0) / 256.0 * 256.0 / np.median(rate)   # quadros desde a virada do tick
    per = 256.0 / np.median(rate)
    hist = np.zeros(int(per) + 1); cnt = np.zeros_like(hist)
    for p, e in zip(ph, envf):
        hist[int(p)] += e; cnt[int(p)] += 1
    prof = hist / np.maximum(cnt, 1)
    print(f'período da batida {per:.2f} quadros; envelope médio por quadro desde a virada do tick:')
    print('  ' + ' '.join(f'{k}:{v:.2f}' for k, v in enumerate(prof)))
    print(f'  pico do pulso {int(np.argmax(prof))} quadros depois da virada do tick')
    # 2. correlação com os transientes do áudio
    w = wave.open(os.path.join(OUT, 'pulso.wav'))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, 2).mean(1) / 32768.0
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / 44100)
    y = np.fft.irfft(X * ((f > 150) & (f < 16000)), len(x))
    spf = a
    e = np.array([np.sqrt((y[int((k - pts[0, 0]) * spf):int((k - pts[0, 0] + 1) * spf)] ** 2).mean() + 1e-12)
                  for k in F if 0 <= (k - pts[0, 0]) * spf and (k - pts[0, 0] + 1) * spf < len(y)])
    n = min(len(e), len(envf))
    on = np.maximum(np.diff(np.log(e[:n])), 0.0)
    ev = envf[1:n] - envf[1:n].mean()
    on = on - on.mean()
    lags = range(-12, 13)
    cc = [float(np.dot(ev[max(0, -L):len(ev) - max(0, L)], on[max(0, L):len(on) - max(0, -L)])) for L in lags]
    best = list(lags)[int(np.argmax(cc))]
    print('correlação envelope x transientes do som por atraso (quadros, + = som depois):')
    print('  ' + ' '.join(f'{L}:{c / max(cc):.2f}' for L, c in zip(lags, cc)))
    print(f'  pico em {best:+d} quadros')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'analise':
        analyse()
    else:
        main()
