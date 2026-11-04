from pathlib import Path
import subprocess, os, json
base=Path(__file__).resolve().parents[2];src=base/'source';ev=base/'evidence'
results=[]
for name,repo,file in [('python_tools','VisionTools','tests/test_python_tools.py'),('logic_engine','VisionTools','tests/test_logic_engine.py'),('package_profile','VisionTools','tests/test_package_profile.py'),('build_metadata_entry','VisionRT','tests/test_build_metadata_integration.py')]:
 env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
 if name=='build_metadata_entry':
  env.pop('CMAKE_COMMAND',None);env.pop('CMAKE_MAKE_PROGRAM',None)
 r=subprocess.run(['python3',file],cwd=src/repo,env=env,capture_output=True,text=True)
 (ev/(name+'.log')).write_text(r.stdout+r.stderr)
 results.append({'name':name,'exit':r.returncode,'tail':(r.stdout+r.stderr)[-1500:]})
(ev/'focused_results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2))
print(json.dumps(results,ensure_ascii=False,indent=2))
