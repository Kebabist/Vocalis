from flask import Flask, request, render_template_string, jsonify
import requests

app = Flask(__name__)

# Simple single-file frontend that records audio, resamples to target samplerate,
# converts to 16-bit PCM and POSTs to this server which forwards it to the ESP.

HTML = """
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Record and Send to ESP</title>
  <style>
    :root{--bg:#0f1724;--card:#0b1220;--accent:#8b5cf6;--muted:#94a3b8;--glass:rgba(255,255,255,0.04);--success:#10b981;--error:#ef4444}
    html,body{height:100%;margin:0;font-family:'Inter',ui-sans-serif,system-ui,Arial,sans-serif;background:linear-gradient(180deg,#071021 0%, #071a2b 60%);color:#e6eef8;overflow-x:hidden}
    .wrap{min-height:100%;display:flex;align-items:center;justify-content:center;padding:1rem}
    .card{width:min(720px,95vw);background:linear-gradient(180deg, rgba(255,255,255,0.02), rgba(255,255,255,0.01));border-radius:16px;padding:20px 24px;box-shadow:0 12px 40px rgba(2,6,23,0.7);border:1px solid rgba(255,255,255,0.03);backdrop-filter:blur(10px)}
    h1{margin:0 0 16px;font-size:24px;font-weight:700;color:var(--accent);text-align:center}
    .row{display:flex;gap:12px;align-items:center;margin-bottom:16px;flex-wrap:wrap;justify-content:space-between}
    label{font-size:14px;color:var(--muted);font-weight:500}
    input[type=text], input[type=number] {background:var(--glass);border:1px solid rgba(255,255,255,0.08);padding:10px 12px;border-radius:10px;color:inherit;width:100%;transition:border-color 0.2s}
    input:focus{border-color:var(--accent);outline:none}
    .controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:16px}
    .controls .col{display:flex;flex-direction:column;gap:6px}
    .options{display:flex;gap:16px;align-items:center;flex-wrap:wrap}
    button.primary{background:linear-gradient(90deg,var(--accent),#06b6d4);border:none;color:#061020;padding:12px 20px;border-radius:12px;font-weight:600;cursor:pointer;transition:transform 0.1s,box-shadow 0.1s}
    button.primary:hover{box-shadow:0 4px 12px rgba(139,92,246,0.3)}
    button.primary:active{transform:translateY(2px)}
    button.primary:disabled{background:#555;color:#999;cursor:not-allowed}
    .status{margin-left:12px;font-size:14px;color:var(--muted)}
    .status.recording{color:var(--success)}
    .status.error{color:var(--error)}
    .waveform-container{margin-bottom:16px}
    .waveform-container{margin-bottom:16px}
    canvas#waveform{width:100%;height:100px;border-radius:12px;background:linear-gradient(90deg, rgba(139,92,246,0.08), rgba(6,182,212,0.02));box-shadow:inset 0 2px 4px rgba(0,0,0,0.1)}
    pre#resp{background:rgba(255,255,255,0.02);padding:16px;border-radius:12px;min-height:200px;overflow:auto;font-size:14px;color:#dff3ff;white-space:pre-wrap;border:1px solid rgba(255,255,255,0.05)}
    .muted{color:var(--muted);font-size:13px}
    .small{font-size:12px;color:var(--muted)}
    @media (max-width:600px){.card{padding:16px 18px}.controls{grid-template-columns:1fr}.options{flex-direction:column;align-items:flex-start}}
  </style>
  </head>
<body>
  <div class="wrap">
  <div class="card">
    <h1>Record and Send to ESP</h1>
    <div class="row">
      <label>ESP Host (ip:port): <input id="espHost" value="10.25.157.232:80" size="24"></label>
      <div class="muted small">Make sure the ESP and this machine are on the same network.</div>
    </div>

    <div class="controls">
      <div class="col">
        <label>Samplerate (Hz)</label>
        <input id="samplerate" value="16000">
      </div>
      <div class="col">
        <label>Duration (s)</label>
        <input id="duration" value="1.0">
      </div>
      <div class="col">
        <label>Target samples</label>
        <input id="targetSamples" value="16000">
      </div>
      <div class="col">
        <label>Auto-calibrate (s)</label>
        <input id="autoCalib" value="0">
      </div>
    </div>

    <div class="row options">
      <label><input type="checkbox" id="useVad"> Use WebRTC VAD</label>
      <label>Aggressiveness: <input id="vadAgg" value="2" size="2"></label>
      <label><input type="checkbox" id="doDenoise"> Denoise</label>
    </div>

    <div class="row">
      <button class="primary" id="recordBtn">🎤 Record</button>
      <div id="status" class="status"></div>
    </div>

    <div class="waveform-container">
      <canvas id="waveform" width="680" height="100"></canvas>
    </div>

    <h3 style="margin:0 0 8px;color:#b6e0ff">Response</h3>
    <pre id="resp"></pre>
  </div>
  </div>

<script>
const defaultTargetSampleRate = {{target_sample_rate}};
let audioContext;
const recordBtn = document.getElementById('recordBtn');
const statusEl = document.getElementById('status');
const respEl = document.getElementById('resp');
const canvas = document.getElementById('waveform');
const canvasCtx = canvas && canvas.getContext ? canvas.getContext('2d') : null;
let analyserNode = null;
let mediaSourceNode = null;
let rafId = null;

async function recordSegment(stream, seconds){
  return new Promise((resolve, reject) => {
    const recorder = new MediaRecorder(stream);
    const parts = [];
    recorder.ondataavailable = e => parts.push(e.data);
    recorder.onstop = () => resolve(new Blob(parts, {type: parts[0]?.type || 'audio/webm'}));
    recorder.onerror = reject;
    recorder.start();
    setTimeout(() => { try{ recorder.stop(); }catch(e){} }, Math.round(seconds*1000));
  });
}

recordBtn.onclick = async () => {
  try{
    recordBtn.disabled = true;
    recordBtn.textContent = '🎤 Recording...';
    const esp = (document.getElementById('espHost') || {}).value?.trim();
    if (!esp) { alert('Enter ESP host'); recordBtn.disabled = false; return; }
    const samplerate = parseInt((document.getElementById('samplerate') || {}).value) || defaultTargetSampleRate;
    const duration = parseFloat((document.getElementById('duration') || {}).value) || 1.0;
    const targetSamples = parseInt((document.getElementById('targetSamples') || {}).value) || samplerate * Math.ceil(duration);
    const useVad = !!(document.getElementById('useVad') && document.getElementById('useVad').checked);
    const vadAgg = (document.getElementById('vadAgg') || {}).value || '2';
    const doDenoise = !!(document.getElementById('doDenoise') && document.getElementById('doDenoise').checked);
    const autoCalib = parseFloat((document.getElementById('autoCalib') || {}).value) || 0.0;

    statusEl.textContent = 'Getting mic...';
    statusEl.className = 'status';
    const stream = await navigator.mediaDevices.getUserMedia({audio:true});
    audioContext = new (window.AudioContext || window.webkitAudioContext)();

    // set up live analyser for waveform preview
    try{
      analyserNode = audioContext.createAnalyser();
      analyserNode.fftSize = 2048;
      mediaSourceNode = audioContext.createMediaStreamSource(stream);
      mediaSourceNode.connect(analyserNode);
    }catch(e){ analyserNode = null; mediaSourceNode = null }

    let noiseBlob = null;
    if (autoCalib > 0) {
      statusEl.textContent = 'Recording noise sample...';
      statusEl.className = 'status';
      noiseBlob = await recordSegment(stream, autoCalib);
    }

    statusEl.textContent = 'Recording...';
    statusEl.className = 'status recording';

    // start live drawing
    if (analyserNode && canvasCtx) startLiveDraw();

    const mainBlob = await recordSegment(stream, duration);

    // stop live drawing
    stopLiveDraw();

    statusEl.textContent = 'Processing...';
    statusEl.className = 'status';

    // decode and resample main
    const mainBuf = await mainBlob.arrayBuffer();
    const decoded = await audioContext.decodeAudioData(mainBuf);
    let rendered = decoded;
    if (decoded.sampleRate !== samplerate) {
      const offlineCtx = new OfflineAudioContext(1, Math.ceil(decoded.duration * samplerate), samplerate);
      const src = offlineCtx.createBufferSource();
      src.buffer = decoded;
      src.connect(offlineCtx.destination);
      src.start(0);
      rendered = await offlineCtx.startRendering();
    }
    const channelData = rendered.numberOfChannels > 0 ? rendered.getChannelData(0) : new Float32Array(rendered.length);
    const buffer = new ArrayBuffer(channelData.length * 2);
    const view = new DataView(buffer);
    let offset = 0;
    for (let i=0;i<channelData.length;i++){
      let s = Math.max(-1, Math.min(1, channelData[i]));
      view.setInt16(offset, s < 0 ? s * 0x8000 : s * 0x7FFF, true);
      offset += 2;
    }

    // optional noise blob processing
    let noiseBytes = null;
    if (noiseBlob) {
      const nbuf = await noiseBlob.arrayBuffer();
      const ndecoded = await audioContext.decodeAudioData(nbuf);
      let nrendered = ndecoded;
      if (ndecoded.sampleRate !== samplerate) {
        const offline = new OfflineAudioContext(1, Math.ceil(ndecoded.duration * samplerate), samplerate);
        const src2 = offline.createBufferSource();
        src2.buffer = ndecoded;
        src2.connect(offline.destination);
        src2.start(0);
        nrendered = await offline.startRendering();
      }
      const nchan = nrendered.numberOfChannels > 0 ? nrendered.getChannelData(0) : new Float32Array(nrendered.length);
      const nbufArr = new ArrayBuffer(nchan.length * 2);
      const nview = new DataView(nbufArr);
      let noff = 0;
      for (let i=0;i<nchan.length;i++){
        let s = Math.max(-1, Math.min(1, nchan[i]));
        nview.setInt16(noff, s < 0 ? s * 0x8000 : s * 0x7FFF, true);
        noff += 2;
      }
      noiseBytes = nbufArr;
    }

    // draw recorded waveform preview (full buffer)
    try{ drawWaveform(channelData); }catch(e){}

    // Build FormData
    const form = new FormData();
    form.append('file', new Blob([buffer], {type:'application/octet-stream'}), 'audio.raw');
    if (noiseBytes) form.append('noise', new Blob([noiseBytes], {type:'application/octet-stream'}), 'noise.raw');
    form.append('target_samples', String(targetSamples));
    form.append('samplerate', String(samplerate));
    form.append('use_vad', useVad ? '1' : '0');
    form.append('vad_aggressiveness', String(vadAgg));
    form.append('denoise', doDenoise ? '1' : '0');

    statusEl.textContent = 'Uploading...';
    statusEl.className = 'status';
    respEl.textContent = '';
    const uploadResp = await fetch('/upload_audio?esp='+encodeURIComponent(esp), {method:'POST', body: form});
    const contentType = uploadResp.headers.get('content-type')||'';
    if (contentType.includes('application/json')) {
      const j = await uploadResp.json();
      respEl.textContent = JSON.stringify(j, null, 2);
    } else {
      const txt = await uploadResp.text();
      respEl.textContent = txt;
    }
    statusEl.textContent = 'Done';
    statusEl.className = 'status';
  }catch(err){
    statusEl.textContent = 'Error';
    statusEl.className = 'status error';
    respEl.textContent = String(err);
  } finally {
    try{ if (audioContext) audioContext.close(); }catch(e){}
    recordBtn.disabled = false;
    recordBtn.textContent = '🎤 Record';
  }
}

function startLiveDraw(){
  if (!analyserNode || !canvasCtx) return;
  const bufferLen = analyserNode.fftSize;
  const data = new Float32Array(bufferLen);
  function draw(){
    analyserNode.getFloatTimeDomainData(data);
    // draw to canvas
    const w = canvas.width; const h = canvas.height;
    canvasCtx.clearRect(0,0,w,h);
    // background
    const grad = canvasCtx.createLinearGradient(0,0,w,0);
    grad.addColorStop(0,'rgba(139,92,246,0.12)'); grad.addColorStop(1,'rgba(6,182,212,0.04)');
    canvasCtx.fillStyle = grad; canvasCtx.fillRect(0,0,w,h);
    canvasCtx.lineWidth = 2; canvasCtx.strokeStyle = 'rgba(255,255,255,0.9)';
    canvasCtx.beginPath();
    const sliceW = w / bufferLen;
    let x = 0;
    for (let i=0;i<bufferLen;i++){
      const v = data[i];
      const y = (1 - (v + 1)/2) * h;
      if (i===0) canvasCtx.moveTo(x,y); else canvasCtx.lineTo(x,y);
      x += sliceW;
    }
    canvasCtx.stroke();
    rafId = requestAnimationFrame(draw);
  }
  rafId = requestAnimationFrame(draw);
}

function stopLiveDraw(){ if (rafId) cancelAnimationFrame(rafId); rafId = null; if (mediaSourceNode && analyserNode) try{ mediaSourceNode.disconnect(); }catch(e){} }

function drawWaveform(float32Data){
  if (!canvasCtx) return;
  const w = canvas.width; const h = canvas.height;
  canvasCtx.clearRect(0,0,w,h);
  const grad = canvasCtx.createLinearGradient(0,0,w,0);
  grad.addColorStop(0,'rgba(139,92,246,0.12)'); grad.addColorStop(1,'rgba(6,182,212,0.04)');
  canvasCtx.fillStyle = grad; canvasCtx.fillRect(0,0,w,h);
  canvasCtx.lineWidth = 2; canvasCtx.strokeStyle = 'rgba(255,255,255,0.95)';
  canvasCtx.beginPath();
  const step = Math.ceil(float32Data.length / w);
  let x = 0;
  for (let i=0;i<float32Data.length;i+=step){
    const v = float32Data[i];
    const y = (1 - (v + 1)/2) * h;
    if (x===0) canvasCtx.moveTo(x,y); else canvasCtx.lineTo(x,y);
    x++;
  }
  canvasCtx.stroke();
}
</script>
</body>
</html>
"""


@app.route('/')
def index():
    return render_template_string(HTML, target_sample_rate=16000)


@app.route('/upload_audio', methods=['POST'])
def upload_audio():
  esp = request.args.get('esp')
  if not esp:
    return jsonify({'error': "Missing 'esp' query param. Use ?esp=IP:PORT"}), 400

  # processing options
  # processing options: accept from args or form
  target_samples = int(request.values.get('target_samples') or 0)
  use_vad = request.values.get('use_vad') == '1'
  vad_aggressiveness = int(request.values.get('vad_aggressiveness') or 2)
  do_denoise = request.values.get('denoise') == '1'
  samplerate = int(request.values.get('samplerate') or 16000)

  # Accept multipart/form-data or raw body
  pcm = None
  noise_pcm = None
  if request.files and 'file' in request.files:
    pcm = request.files['file'].read()
    if 'noise' in request.files:
      noise_pcm = request.files['noise'].read()
  else:
    pcm = request.get_data()

  if not pcm:
    return jsonify({'error': 'No audio bytes received'}), 400

  # Convert bytes to numpy int16 array for processing
  try:
    import numpy as np
    arr = np.frombuffer(pcm, dtype=np.int16)
  except Exception:
    arr = None

  sr = samplerate

  # Pad or truncate to target samples if requested
  if arr is not None and target_samples and target_samples > 0:
    if arr.size > target_samples:
      arr = arr[:target_samples]
    elif arr.size < target_samples:
      pad = np.zeros(target_samples - arr.size, dtype=np.int16)
      arr = np.concatenate([arr, pad])

  # Optional VAD check
  if use_vad and arr is not None:
    try:
      import webrtcvad
      vad = webrtcvad.Vad(vad_aggressiveness)
      frame_ms = 30
      frame_len = int(sr * frame_ms / 1000)
      frame_bytes = frame_len * 2
      pcm_bytes = arr.tobytes()
      if len(pcm_bytes) % frame_bytes != 0:
        pcm_bytes += b"\x00" * (frame_bytes - (len(pcm_bytes) % frame_bytes))
      total_frames = 0
      speech_frames = 0
      for i in range(0, len(pcm_bytes), frame_bytes):
        frame = pcm_bytes[i:i+frame_bytes]
        if len(frame) != frame_bytes:
          continue
        total_frames += 1
        try:
          if vad.is_speech(frame, sr):
            speech_frames += 1
        except Exception:
          pass
      # require at least 2 speech frames by default
      if speech_frames < 2:
        return jsonify({'error': 'VAD rejected: not enough speech frames', 'speech_frames': speech_frames}), 400
    except Exception as e:
      # if webrtcvad not installed or fails, continue and warn
      print('VAD skipped:', e)

  # Optional denoise (using noisereduce) — use provided noise sample if available
  if do_denoise and arr is not None:
    try:
      import noisereduce as nr
      audio_f = arr.astype('float32') / 32768.0
      if noise_pcm:
        noise_arr = np.frombuffer(noise_pcm, dtype=np.int16).astype('float32') / 32768.0
        reduced = nr.reduce_noise(y=audio_f, y_noise=noise_arr, sr=sr)
      else:
        reduced = nr.reduce_noise(y=audio_f, sr=sr)
      # convert back to int16
      clipped = np.clip(reduced * 32768.0, -32768, 32767).astype(np.int16)
      arr = clipped
    except Exception as e:
      print('Denoise skipped:', e)

  # reconstruct bytes to send
  out_bytes = arr.tobytes() if arr is not None else pcm

  # Forward as multipart/form-data to ESP /upload endpoint
  try:
    url = f'http://{esp}/upload'
    files = {'file': ('audio.raw', out_bytes, 'application/octet-stream')}
    r = requests.post(url, files=files, timeout=15)
    # Return ESP response content and status
    content_type = r.headers.get('Content-Type', 'application/octet-stream')
    return (r.content, r.status_code, {'Content-Type': content_type})
  except requests.exceptions.RequestException as e:
    return jsonify({'error': 'Failed to contact ESP', 'details': str(e)}), 502


if __name__ == '__main__':
    print('Starting mic->ESP webserver on http://127.0.0.1:5000')
    app.run(host='0.0.0.0', port=5000)
