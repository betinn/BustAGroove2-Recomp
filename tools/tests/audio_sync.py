#!/usr/bin/env python3
"""Ritmo da música no recompilado: deriva entre o som e o tempo do jogo, e latência do áudio no PC (build-debug).

O jogo julga os comandos pelo tick da música (0x8005FBB4, 256 por batida); quem joga aperta pelo som. Numa partida
inteira este script:
  1. amostra o quadro e o tick o mais rápido possível e grava o som do SPU (em blocos, o anel guarda ~95 s);
  2. lê o `audio_stats` a cada ~1 s: enchimento do buffer de saída do PC (fill_ms) e alvo (target_ms);
  3. relata, em janelas de 20 s, o atraso (quadros) que melhor alinha os transientes do som com a virada da batida
     do tick. Atraso constante = sem deriva no domínio emulado (o som e o tick andam juntos); atraso que cresce ou
     diminui = deriva (o ritmo sai do lugar durante a música);
  4. amostras do SPU por quadro do jogo (esperado 44100 / 59,94 = 735,7).
A latência que quem joga sente a mais que no console = buffer de saída (fill_ms) + buffer do dispositivo + vídeo.
Rota: 2 jogadores (BAG2_MATCH, padrão HEAT x COMET no palco do HEAT): mesma música e palco entre as variantes.
Uso: python tools/tests/audio_sync.py [segundos] [ciclos do vblank, comando vblank_ntsc] | analise [rótulo]
Saída: logs/remake/ritmo/ritmo_<rótulo>.json (rótulo = ciclos pedidos ou "padrao").
"""
from __future__ import annotations

import json
import os
import sys
import time
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'remake', 'ritmo')
MUSIC_TICK = 0x8005FBB4


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 170.0
    tag = sys.argv[2] if len(sys.argv) > 2 else 'padrao'
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='ritmo')
    s.start()
    scratch = os.path.join(OUT, 'marca.wav').replace('\\', '/')
    try:
        if len(sys.argv) > 2:
            print(s.cmd({'cmd': 'mod_cmd', 'name': 'vblank_ntsc', 'args': sys.argv[2]}).get('reply'), flush=True)
        s.enter_match()
        print(s.cmd({'cmd': 'mod_cmd', 'name': 'vblank_ntsc', 'args': ''}).get('reply'), flush=True)
        rows, fills, marks, chunks = [], [], [], []

        def mark():
            f = s.cmd({'cmd': 'frame'})['frame']
            r = s.cmd({'cmd': 'audio_wav', 'path': scratch, 'count': 1})
            marks.append((f, int(r['tap_total'])))

        def dump():
            a0 = marks[-2][1] if len(marks) > 1 else marks[0][1]
            p = os.path.join(OUT, f'ritmo_{tag}_{len(chunks)}.wav').replace('\\', '/')
            r = s.cmd({'cmd': 'audio_wav', 'path': p, 'start': str(a0), 'count': str(marks[-1][1] - a0)}, 60)
            chunks.append({'path': p, 'start': a0, 'count': marks[-1][1] - a0, 'reply': r})

        mark()
        t0 = time.time(); tf = 0.0; td = t0
        while time.time() - t0 < secs:
            f = s.cmd({'cmd': 'frame'})['frame']
            tick = int.from_bytes(bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': MUSIC_TICK, 'len': 4})['hex']), 'little')
            rows.append((f, tick))
            if time.time() - tf > 1.0:
                tf = time.time()
                a = s.cmd({'cmd': 'audio_stats'})
                o = a.get('out', {})
                fills.append((f, o.get('fill_ms'), o.get('target_ms'), o.get('underruns'), o.get('host_rate')))
            if time.time() - td > 55.0:
                td = time.time(); mark(); dump()
        mark(); dump()
        with open(os.path.join(OUT, f'ritmo_{tag}.json'), 'w', encoding='utf-8') as fp:
            json.dump({'rows': rows, 'fills': fills, 'marks': marks, 'chunks': chunks}, fp)
        print('amostras', len(rows), s.fps())
    finally:
        s.stop()
    analyse(tag)


def analyse(tag='padrao'):
    J = json.load(open(os.path.join(OUT, f'ritmo_{tag}.json'), encoding='utf-8'))
    R = np.array(J['rows'], float)
    marks = np.array(J['marks'], float)
    spf = (marks[-1, 1] - marks[0, 1]) / (marks[-1, 0] - marks[0, 0])
    print(f'amostras do SPU por quadro do jogo: {spf:.2f} (esperado 735,7 a 44,1 kHz / 59,94 Hz)')
    F = [x for x in J['fills'] if x[1] is not None]
    if F:
        fm = np.array([x[1] for x in F], float)
        print(f'buffer de saída do PC: fill_ms méd {fm.mean():.1f} mín {fm.min():.1f} máx {fm.max():.1f}, '
              f'alvo {F[0][2]} ms, taxa do dispositivo {F[0][4]} Hz, underruns {F[-1][3]}')
    # som do SPU, emendado, alinhado por quadro
    x = []
    for c in J['chunks']:
        w = wave.open(c['path'])
        x.append(np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, 2).mean(1) / 32768.0)
    x = np.concatenate(x)
    X = np.fft.rfft(x); fq = np.fft.rfftfreq(len(x), 1 / 44100)
    y = np.fft.irfft(X * ((fq > 150) & (fq < 16000)), len(x))
    f0 = marks[0, 0]
    fr, tick = R[:, 0], R[:, 1]
    Fr = np.arange(int(fr[0]) + 1, int(fr[-1]))
    tk = np.interp(Fr, fr, tick)
    keep = [(k, t) for k, t in zip(Fr, tk) if 0 <= (k - f0) * spf and (k - f0 + 1) * spf < len(y)]
    Fr = np.array([k for k, _ in keep]); tk = np.array([t for _, t in keep])
    e = np.array([np.sqrt((y[int((k - f0) * spf):int((k - f0 + 1) * spf)] ** 2).mean() + 1e-12) for k in Fr])
    on = np.concatenate([[0.0], np.maximum(np.diff(np.log(e)), 0.0)])
    rate = np.median(np.diff(tk)[np.diff(tk) > 0]) / 2.0     # o jogo roda a 30 Hz: o tick anda de 2 em 2 quadros
    phase = (tk % 256.0) / 256.0
    env = np.exp(-7.0 * phase)                               # pulso na virada da batida do tick, sem atraso
    win = 1200                                               # 20 s
    print('atraso som x tick por janela de 20 s (quadros; + = o som vem depois da virada do tick):')
    res = []
    for w0 in range(0, len(Fr) - win // 2, win):
        sl = slice(w0, min(w0 + win, len(Fr)))
        if tk[sl][-1] - tk[sl][0] < 0.8 * (sl.stop - sl.start) * rate:   # música parada ou acabando
            continue
        ev = env[sl] - env[sl].mean(); oo = on[sl] - on[sl].mean()
        lags = list(range(-6, 25))
        cc = [float(np.dot(ev[max(0, -L):len(ev) - max(0, L)], oo[max(0, L):len(oo) - max(0, -L)])) for L in lags]
        b = int(np.argmax(cc))
        # pico com interpolação parabólica (subquadro)
        frac = 0.0
        if 0 < b < len(cc) - 1:
            d = cc[b - 1] - 2 * cc[b] + cc[b + 1]
            frac = 0.5 * (cc[b - 1] - cc[b + 1]) / d if d else 0.0
        bpm = 60.0 * 59.94 * (tk[sl][-1] - tk[sl][0]) / (sl.stop - sl.start) / 256.0
        res.append((Fr[sl][0] - f0, lags[b] + frac, cc[b] / (np.linalg.norm(ev) * np.linalg.norm(oo) + 1e-12), bpm))
        print(f'  quadro {Fr[sl][0] - f0:6.0f}: atraso {lags[b] + frac:+5.2f} (força {res[-1][2]:.2f}) ~{bpm:.0f} BPM')
    if len(res) > 1:
        L = np.array([r[1] for r in res])
        print(f'  deriva: {L[-1] - L[0]:+.2f} quadros do começo ao fim; desvio {L.std():.2f}')
    # Fase circular (fase 4): onde caem os ataques do som dentro da batida do tick, média vetorial ponderada pela
    # força do ataque, por janela. Não escolhe pico (a correlação acima pula entre tempo e contratempo nas músicas
    # de ataque fraco); a 2ª harmônica (meia batida) é estável mesmo se o som marca o contratempo. Deriva = reta
    # pelas fases desembrulhadas.
    fpb = 256.0 / rate                                       # quadros por batida
    print(f'fase circular dos ataques na batida do tick ({fpb:.1f} quadros por batida; + = som depois do tick):')
    out = []
    for w0 in range(0, len(Fr) - win // 2, win):
        sl = slice(w0, min(w0 + win, len(Fr)))
        if tk[sl][-1] - tk[sl][0] < 0.8 * (sl.stop - sl.start) * rate:
            continue
        w = on[sl]
        z1 = np.sum(w * np.exp(2j * np.pi * phase[sl])) / (w.sum() + 1e-12)
        z2 = np.sum(w * np.exp(4j * np.pi * phase[sl])) / (w.sum() + 1e-12)
        out.append((Fr[sl][0] - f0, np.angle(z1), abs(z1), np.angle(z2), abs(z2)))
    if out:
        o = np.array(out)
        d1 = np.unwrap(o[:, 1]) / (2 * np.pi) * fpb
        d2 = np.unwrap(o[:, 3]) / (2 * np.pi) * fpb / 2
        for r, a, b in zip(o, d1, d2):
            print(f'  quadro {r[0]:6.0f}: batida {a:+6.2f} (força {r[2]:.2f}) | meia batida {b:+6.2f} (força {r[4]:.2f})')
        if len(o) > 2:
            t20 = o[:, 0] / 1200.0
            print(f'  deriva por 20 s (reta): batida {np.polyfit(t20, d1, 1)[0]:+.2f} quadros, '
                  f'meia batida {np.polyfit(t20, d2, 1)[0]:+.2f} quadros')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'analise':
        analyse(sys.argv[2] if len(sys.argv) > 2 else 'padrao')
    else:
        main()
