"""Actual clients: shoot through P2P, enter a room code on the keypad, choose Original and equip both players.

Only starts its own hidden clients, fresh runtime folders and an ephemeral local lobby. No live service needed.
"""
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from netplay_relay_smoke import REPO, lobby


def run():
    output = Path(tempfile.mkdtemp(prefix='hotd2_menu_pointer_'))
    server = lobby.Server('127.0.0.1', 0)
    stop = threading.Event()
    def service():
        while not stop.is_set():
            try:
                data, addr = server.sock.recvfrom(65536)
                server.handle(data, addr)
            except (TimeoutError, ConnectionResetError):
                server.expire()
    worker = threading.Thread(target=service, daemon=True); worker.start()
    env = {k:v for k,v in os.environ.items() if not k.startswith('HOTD2_')}
    env.update(HOTD2_PLAY_MENU='1', HOTD2_NET_NODIRECT='1', HOTD2_NET_LOG='1')
    processes, logs = [], []
    def event(frame, x, y, buttons=1, player=0):
        return f'{frame} {player} {x/640} {y/480} {buttons}\n{frame+4} {player} {x/640} {y/480} 0\n'
    base = event(100,320,240) + event(200,320,240,4) + event(300,320,240,4)
    base += event(400,40,338) + event(450,320,202)
    def launch(role, script):
        folder=output/role; folder.mkdir(); (folder/'input.txt').write_text(script)
        log=(output/(role+'.log')).open('w'); logs.append(log)
        args=[str(REPO/'build/src/port/Release/play.exe'),str(REPO/'game'),'--scene',str(REPO/'scenes/st1_play.scene'),
              '--script',str(folder/'input.txt'),'--pace','--trace','--screenshot',str(folder/'game.png'),
              '--frame','2200','--port','0','--lobby',f'127.0.0.1:{server.sock.getsockname()[1]}','--width','640','--height','480']
        client_env=dict(env, HOTD2_PLAY_ZLOG=str(folder/'world.zlog'))
        processes.append(subprocess.Popen(args,cwd=folder,env=client_env,stdout=log,stderr=subprocess.STDOUT,
                                          creationflags=subprocess.CREATE_NO_WINDOW))
    try:
        launch('host',base+event(500,320,142)+event(1000,240,172)+event(1300,80,90)+event(1500,550,168))
        deadline=time.monotonic()+25
        while not server.rooms:
            if time.monotonic()>deadline: raise TimeoutError(f'Host room missing: {output}')
            time.sleep(.1)
        code=str(next(iter(server.rooms.values())).code).zfill(5)
        join=base+event(500,320,172)
        for i,digit in enumerate(code):
            index=(int(digit)+9)%10
            join+=event(600+i*35,200+index%5*56,286+index//5*36)
        join+=event(1300,80,122,player=1)+event(1500,550,296,player=1)
        launch('join',join)
        deadline=time.monotonic()+65
        while any(p.poll() is None for p in processes):
            if time.monotonic()>deadline: raise TimeoutError(f'Clients unfinished: {output}')
            time.sleep(.2)
        assert all(p.returncode==0 for p in processes)
    finally:
        for p in processes:
            if p.poll() is None: p.kill(); p.wait()
        for log in logs: log.close()
        stop.set(); worker.join(timeout=2); server.sock.close()
    results={}
    hashes=[]
    for role,p in [('host',1),('join',2)]:
        text=(output/(role+'.log')).read_text()
        assert f'connected as P{p} (lobby)' in text and 'via server relay' in text, text[-4000:]
        assert 'network room: host selected 1' in text and 'frame 2200 mode 6' in text, text[-4000:]
        assert 'DESYNC' not in text and 'continuing alone' not in text, text[-4000:]
        hashes.append(re.search(r'final state hash ([0-9A-F]+) at frame 2200', text)[1])
        results[role]=[line for line in text.splitlines() if 'connected as' in line or 'frame 2200' in line]
    assert hashes[0] == hashes[1], hashes
    report={'passed':True,'output':str(output),'final_world_hash':hashes[0],'results':results}
    (output/'verification.json').write_text(json.dumps(report,indent=2)); print(json.dumps(report,indent=2))
    return report

if __name__=='__main__': run()
