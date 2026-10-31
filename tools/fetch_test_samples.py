from pathlib import Path
import urllib.request,json,hashlib
root=Path('build/external');manifest=[]
for model in ['Fox','WaterBottle']:
    base=f'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/8f492390ddc7a2ae221a1369ef560589cf8c70fb/Models/{model}'
    for suffix,name in [(f'/glTF-Binary/{model}.glb',f'{model}.glb'),('/README.md',f'{model}.README.md')]:
        data=urllib.request.urlopen(base+suffix,timeout=45).read();(root/name).write_bytes(data)
        manifest.append({'url':base+suffix,'file':name,'sha256':hashlib.sha256(data).hexdigest()})
(root/'provenance.json').write_text(json.dumps(manifest,indent=2))
print('Downloaded Khronos sample models into ignored build/external for compatibility testing')
