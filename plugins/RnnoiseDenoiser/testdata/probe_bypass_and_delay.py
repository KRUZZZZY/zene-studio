
import numpy as np, struct, os
TD="/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
CC=os.environ.get("RNNOISE_REPEAT_DIR", "/tmp/crashcheck")
def read_wav(p):
    raw=open(p,"rb").read();pos=12;fmt=None;data=None
    while pos+8<=len(raw):
        cid,size=struct.unpack_from("<4sI",raw,pos);ch=raw[pos+8:pos+8+size]
        if cid==b"fmt ": fmt=struct.unpack_from("<HHIIHH",ch,0)
        elif cid==b"data": data=ch
        pos+=8+size+(size&1)
    tag,nch,sr,_,_,bits=fmt
    x=np.frombuffer(data,dtype="<f4" if tag==3 else "<i2").astype(np.float64)
    if tag==1: x/=32768.0
    return x.reshape(-1,nch)
def mono(x): return (x[:,0]+x[:,1])*0.5
def db(v): return 20*np.log10(v) if v>0 else float("-inf")
A=mono(read_wav(f"{TD}/out_A_denoised.wav")); B=mono(read_wav(f"{TD}/out_B_bypassed.wav"))
C=mono(read_wav(f"{TD}/out_C_nofx.wav")); F=mono(read_wav(f"{TD}/out_F_bogus.wav"))
C1=mono(read_wav(f"{CC}/C_1.wav")); C3=mono(read_wav(f"{CC}/C_3.wav"))
print("=== B vs C: outside the 70ms startup window ===")
d=np.abs(B-C); print(f"  frames >=3360: max|diff|={d[3360:].max():.10f}  (whole file max {d.max():.6f})")
print("=== F vs C: outside startup ===")
d=np.abs(F-C); print(f"  frames >=3360: max|diff|={d[3360:].max():.10f}  (whole file max {d.max():.6f})")
print("=== C_1 vs C_3 (same project, two renders) ===")
d=np.abs(C1-C3); print(f"  whole file max|diff|={d.max():.6f}   frames>=3360 max|diff|={d[3360:].max():.10f}")
print()
print("=== A vs C: delayed-copy residual (A[n] vs C[n-1439]) ===")
for lo,hi,name in [(0.10,0.70,"speech1"),(0.85,1.15,"noise-only"),(1.30,1.90,"speech2")]:
    s,e=int(lo*48000),int(hi*48000)
    a=A[s:e]; c=C[s-1439:e-1439]
    res=a-c
    print(f"  {name:11s}: signal rms={db(np.sqrt(np.mean(a**2))):7.2f} dB, residual rms={db(np.sqrt(np.mean(res**2))):7.2f} dB, residual rel signal={db(np.sqrt(np.mean(res**2))/np.sqrt(np.mean(a**2))):+6.2f} dB, max|res|={np.max(np.abs(res)):.6f}")
print()
print("=== E spread (float-scale denoiser runs) ===")
for p,label in [(f"{TD}/out_E_float_denoised.wav","E suite"),(f"{CC}/E_1.wav","E_1"),(f"{CC}/E_2.wav","E_2"),(f"{CC}/E_3.wav","E_3")]:
    x=mono(read_wav(p)); s,e=int(0.85*48000),int(1.15*48000)
    print(f"  {label}: noise-only rms={np.sqrt(np.mean(x[s:e]**2)):8.4f} ({db(np.sqrt(np.mean(x[s:e]**2))):6.2f} dB)")
print()
print("=== render determinism (sha256 of the WAV data chunk, first 16 hex) ===")
import hashlib
def data_sha(p):
    raw=open(p,"rb").read();pos=12;data=None
    while pos+8<=len(raw):
        cid,size=struct.unpack_from("<4sI",raw,pos)
        if cid==b"data": data=raw[pos+8:pos+8+size]
        pos+=8+size+(size&1)
    return hashlib.sha256(data).hexdigest()[:16]
for tag in ["A","C","D","E"]:
    row=[]
    for i in [1,2,3]:
        try: row.append(f"{tag}_{i}={data_sha(f'{CC}/{tag}_{i}.wav')}")
        except Exception as e: row.append(f"{tag}_{i}=ERR({e})")
    print("  " + "  ".join(row))
