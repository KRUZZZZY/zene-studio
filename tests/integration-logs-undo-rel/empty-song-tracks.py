import json,sys
from pathlib import Path
HERE=Path.cwd()
sys.path.insert(0,str(HERE/"tools/mcp-zene-control")); sys.path.insert(0,str(HERE/"tools/mcp-zene-control/tests"))
import harness as H
i=H.Instance()
try:
    i.wait_engine()
    c=H.RawDawClient(i.socket_path)
    out={}
    out["ping"]=c.call("control.ping")
    out["project_get_state"]=c.call("project.get_state")
    out["track_list"]=c.call("track.list")
    c.close()
    print(json.dumps(out,indent=1,default=str)[:4000])
finally:
    i.cleanup()
