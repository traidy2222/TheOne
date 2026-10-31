from pathlib import Path
import json
root=Path('build/external');fixture=json.loads(Path('assets/sandbox.one').read_text())
for e in fixture['entities']:
    if e.get('asset'):e['asset']=str((Path('assets')/e['asset']).resolve())
fixture['entities'] += [{'id':100,'name':'Khronos Fox / 3 clips','asset':str((root/'Fox.glb').resolve()),'position':[-4,0,1],'scale':[.04,.04,.04],'clip':1},
 {'id':101,'name':'Khronos PBR WaterBottle','asset':str((root/'WaterBottle.glb').resolve()),'position':[3,1.5,0],'scale':[8,8,8]}]
(root/'compatibility.one').write_text(json.dumps(fixture,indent=2))
