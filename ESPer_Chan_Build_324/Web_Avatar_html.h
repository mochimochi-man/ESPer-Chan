#ifndef WEB_AVATAR_HTML_H
#define WEB_AVATAR_HTML_H

// ============================================
// HTML Templates (PROGMEM)
// ============================================
static const char MUSIC_PLAYER_HTML[] PROGMEM = R"MUSIC_HTML(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Music Player - ESPer-Chan</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#fff; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:100vh; font-family:Arial,sans-serif; }
  #face-wrap { width:320px; height:240px; border:2px solid #e0e0e0; border-radius:12px; background:#fafafa; overflow:hidden; margin-bottom:12px; box-shadow: 0 2px 8px rgba(0,0,0,0.06); }
  #face { display:block; width:100%; height:100%; object-fit:cover; }
  .info { text-align:center; margin:4px 0; width:320px; }
  .track { font-size:18px; font-weight:bold; color:#333; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .artist { font-size:14px; color:#666; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .album { font-size:12px; color:#999; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .controls { display:flex; gap:12px; margin:12px 0; align-items:center; justify-content:center; }
  .vol-btn { padding:2px 16px; font-size:28px; font-weight:bold; background:#fff; color:#B71C1C; border:none; border-radius:6px; cursor:pointer; line-height:1.2; }
  .vol-btn:active { transform:scale(0.92); }
  .mode-img-btn { width:48px; height:48px; cursor:pointer; border-radius:50%; opacity:0.85; border:none; display:block; background:transparent; padding:0; }
  .mode-img-btn:active { transform:scale(0.92); }
  .mode-img-btn.active { opacity:1.0; border:2px solid #4CAF50; }
  .idx-btn { background:#FF9800; color:#fff; border:none; padding:6px 12px; border-radius:6px; cursor:pointer; }
  .tip-wrap { position:relative; display:inline-block; }
  .tip-wrap::after { content:attr(data-tip); position:absolute; top:110%; right:0; background:#333; color:#fff; font-size:12px; padding:7px 10px; border-radius:6px; white-space:normal; width:240px; line-height:1.5; opacity:0; pointer-events:none; transition:opacity 0.15s; z-index:200; }
  .tip-wrap:hover::after { opacity:1; }
  @keyframes spin { to { transform:rotate(360deg); } }
  #indexOverlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,0.78); z-index:9999; flex-direction:column; align-items:center; justify-content:center; color:#fff; gap:16px; }
  #indexSpinner { width:40px; height:40px; border:4px solid rgba(255,255,255,0.25); border-top-color:#fff; border-radius:50%; animation:spin 0.9s linear infinite; }
</style>
</head>
<body>
  <div id="top-right-bar" style="position:fixed;top:10px;right:10px;display:flex;align-items:center;gap:10px;z-index:100;background:rgba(255,255,255,0.92);padding:6px 12px;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,0.08);">
    <div class="tip-wrap" data-tip="SDカードに曲リストがないときや新しい曲を追加したときはINDEXボタンを押して曲リストを作成してください。">
      <button class="idx-btn" style="margin:0;" onclick="sendCmd('/index')">INDEX</button>
    </div>
    <span style="font-size:13px;color:#666;cursor:pointer;user-select:none;" onclick="sendCmd('/exit')">[ 通常モードに戻る ]</span>
  </div>
  <div id="face-wrap"><img id="face" src="" alt="face"></div>
  <div class="info track" id="track">停止中</div>
  <div class="info artist" id="artist">-</div>
  <div class="info album" id="album">-</div>
  <div class="controls">
    <img src="data:image/png;base64,__ICON_PREV__" onclick="sendCmd('/prev')" style="width:44px;height:44px;cursor:pointer;" alt="prev">
    <img src="data:image/png;base64,__ICON_PLAY__" id="playImg" onclick="togglePlay()" style="width:56px;height:56px;cursor:pointer;" alt="play">
    <img src="data:image/png;base64,__ICON_NEXT__" onclick="sendCmd('/next')" style="width:44px;height:44px;cursor:pointer;" alt="next">
  </div>
  <div class="controls">
    <button class="vol-btn" onclick="sendCmd('/vol ' + Math.max(0, currentVolume - 1))">−</button>
    <span id="volume" style="font-size:16px;color:#333;min-width:70px;text-align:center;font-weight:bold;">Vol: 15</span>
    <button class="vol-btn" onclick="sendCmd('/vol ' + Math.min(21, currentVolume + 1))">+</button>
  </div>
  <div class="controls">
    <img src="data:image/png;base64,__ICON_MODE_IDX__" id="modeIdx" onclick="sendCmd('/play')" class="mode-img-btn" alt="index">
    <img src="data:image/png;base64,__ICON_MODE_RND__" id="modeRnd" onclick="sendCmd('/random')" class="mode-img-btn" alt="random">
    <img src="data:image/png;base64,__ICON_MODE_ART__" id="modeArt" onclick="sendCmd('/artist')" class="mode-img-btn" alt="artist">
  </div>
<div id="indexOverlay">
  <div style="font-size:20px;font-weight:bold;">INDEX生成中...</div>
  <div style="font-size:13px;color:#bbb;">完了まで数秒〜数分かかります</div>
  <div id="indexSpinner"></div>
</div>
<script>
const PAGE_MODE = 'music';
let isPausedState = false, isPlayingState = false, currentVolume = 15;
let lastFaceIdx = -1, currentFaceBlobUrl = null;
let isIndexGenerating = false, indexSafetyTimer = null;
function showIndexOverlay() {
  isIndexGenerating = true;
  document.getElementById('indexOverlay').style.display = 'flex';
  if (indexSafetyTimer) clearTimeout(indexSafetyTimer);
  indexSafetyTimer = setTimeout(hideIndexOverlay, 300000); // 5分で強制解除
}
function hideIndexOverlay() {
  isIndexGenerating = false;
  document.getElementById('indexOverlay').style.display = 'none';
  if (indexSafetyTimer) { clearTimeout(indexSafetyTimer); indexSafetyTimer = null; }
}
async function checkMode() {
  try { const res = await fetch('/api/mode', {cache:'no-store'}); if (await res.text() !== PAGE_MODE) location.reload(); } catch(e) {}
}
setInterval(checkMode, 5000);
async function updateFace() {
  try {
    const idxRes = await fetch('/api/face', {cache:'no-store'});
    const idx = parseInt(await idxRes.text());
    if (idx !== lastFaceIdx) {
      lastFaceIdx = idx;
      const res = await fetch('/face.gif?t=' + Date.now(), {cache:'no-store'});
      if (!res.ok) return;
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const face = document.getElementById('face');
      face.onload = () => { if (currentFaceBlobUrl) URL.revokeObjectURL(currentFaceBlobUrl); currentFaceBlobUrl = url; };
      face.onerror = () => { URL.revokeObjectURL(url); };
      face.src = url;
    }
  } catch(e) {}
}
async function updateStatus() {
  try {
    const res = await fetch('/api/music/status', {cache:'no-store'});
    const data = await res.json();
    document.getElementById('track').textContent = data.track || '停止中';
    document.getElementById('artist').textContent = data.artist || '-';
    document.getElementById('album').textContent = data.album || '-';
    isPausedState = data.paused; isPlayingState = data.playing;
    const playImg = document.getElementById('playImg');
    playImg.src = 'data:image/png;base64,' + (isPausedState ? '__ICON_PLAY__' : (isPlayingState ? '__ICON_PAUSE__' : '__ICON_PLAY__'));
    document.querySelectorAll('.mode-img-btn').forEach(b => b.classList.remove('active'));
    if (data.mode === 'INDEX') document.getElementById('modeIdx').classList.add('active');
    else if (data.mode === 'RANDOM') document.getElementById('modeRnd').classList.add('active');
    else if (data.mode === 'ARTIST') document.getElementById('modeArt').classList.add('active');
    if (data.volume !== undefined) { currentVolume = data.volume; document.getElementById('volume').textContent = 'Vol: ' + currentVolume; }
    if (isIndexGenerating) hideIndexOverlay(); // サーバー応答 = 生成完了
  } catch(e) { if (!isIndexGenerating) console.error(e); }
}
async function sendCmd(cmd) {
  const isIdx = cmd === '/index';
  if (isIdx) showIndexOverlay();
  try {
    // /index は生成完了までサーバーが応答しないので await でそのままブロック
    await fetch('/api/music/command?cmd=' + encodeURIComponent(cmd), {method:'POST'});
    if (isIdx) hideIndexOverlay();
    setTimeout(updateStatus, 300); setTimeout(updateFace, 400);
  } catch(e) {
    if (!isIdx) console.error(e);
    // /index のfetch失敗はサーバービジーの可能性: safety timerで解除
  }
}
function togglePlay() {
  if (isPausedState) sendCmd('/resume'); else if (isPlayingState) sendCmd('/pause'); else sendCmd('/play');
}
updateFace(); setInterval(updateFace, 800);
updateStatus(); setInterval(updateStatus, 2000);
</script>
</body>
</html>
)MUSIC_HTML";

static const char CAMERA_HTML[] PROGMEM = R"CAMERA_HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESPer-Chan WebCamera</title>
<style>
body{font-family:Arial;background:#fff;color:#333;text-align:center;margin:0}
#top-bar{position:fixed;top:10px;right:10px;z-index:100;background:rgba(255,255,255,0.92);padding:6px 12px;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,0.08);cursor:pointer;font-size:13px;color:#666;}
#container{position:relative;display:inline-block;margin:10px}
#canvas{max-width:100%;width:320px;height:240px;background:#000;border:2px solid #000;border-radius:8px}
#stats{margin:10px;font-size:12px;color:#666}
button{padding:8px 16px;margin:5px;border:none;border-radius:5px;background:#e0e0e0;color:#000;font-weight:bold;cursor:pointer}
#loading{color:#666;font-size:14px;margin:10px}
</style>
<script src="https://cdn.jsdelivr.net/npm/@mediapipe/face_detection@0.4/face_detection.js" crossorigin="anonymous"></script>
</head>
<body>
<div id="top-bar" onclick="sendExit()">[ 通常モードに戻る ]</div>
<div id="loading">Loading MediaPipe model... (first time only, ~2MB)</div>
<div id="stats">Initializing...</div>
<div id="container">
  <canvas id="canvas" width="320" height="240"></canvas>
</div>
<div><button onclick="start()">Start</button><button onclick="stop()">Stop</button></div>
<script>
const canvas=document.getElementById('canvas'),ctx=canvas.getContext('2d'),stats=document.getElementById('stats'),loading=document.getElementById('loading');
let faceDetection=null,streaming=false,isProcessing=false,lastTime=0,frameCount=0,currentBitmap=null,lastPostTime=0,intervalSec=5;

fetch('/interval').then(r=>r.text()).then(v=>{intervalSec=parseInt(v)||5;}).catch(()=>{});

function getTimeObj(){const d=new Date();return{year:d.getFullYear(),month:d.getMonth()+1,day:d.getDate(),hour:d.getHours(),minute:d.getMinutes(),second:d.getSeconds(),ms:d.getMilliseconds(),iso:d.toISOString()};}

function getBox(det){
  const b=det.boundingBox;
  if(b){
    if(b.xMin!==undefined) return{xMin:b.xMin,yMin:b.yMin,width:b.width,height:b.height};
    if(b.xCenter!==undefined) return{xMin:b.xCenter-b.width/2,yMin:b.yCenter-b.height/2,width:b.width,height:b.height};
    if(b.xmin!==undefined) return{xMin:b.xmin,yMin:b.ymin,width:b.width,height:b.height};
  }
  const r=det.locationData&&det.locationData.relativeBoundingBox;
  if(r) return{xMin:r.xmin,yMin:r.ymin,width:r.width,height:r.height};
  return null;
}

function getScore(det){
  if(!det) return null;
  if(det.V&&det.V.length>0&&typeof det.V[0].ga==='number') return det.V[0].ga;
  if(Array.isArray(det.score)&&det.score.length>0&&typeof det.score[0]==='number') return det.score[0];
  if(typeof det.score==='number') return det.score;
  if(det.categories&&det.categories.length>0&&typeof det.categories[0].score==='number') return det.categories[0].score;
  return null;
}

async function sendExit(){try{await fetch('/api/camera/exit',{method:'POST'});}catch(e){}}

async function initMediaPipe(){
  faceDetection=new FaceDetection({locateFile:(file)=>'https://cdn.jsdelivr.net/npm/@mediapipe/face_detection@0.4/'+file});
  faceDetection.setOptions({model:'short',minDetectionConfidence:0.5});
  faceDetection.onResults(onResults);
  loading.style.display='none';
  stats.textContent='MediaPipe ready. Click Start.';
}

function onResults(results){
  try{
    const now=performance.now();
    frameCount++;
    if(now-lastTime>=1000){
      stats.textContent='FPS: '+frameCount+' | Faces: '+(results.detections?results.detections.length:0);
      frameCount=0;lastTime=now;
    }
    if(currentBitmap){ctx.drawImage(currentBitmap,0,0,canvas.width,canvas.height);}
    if(results.detections&&results.detections.length>0){
      results.detections.forEach(det=>{
        const box=getBox(det);if(!box) return;
        const x=box.xMin*canvas.width,y=box.yMin*canvas.height,w=box.width*canvas.width,h=box.height*canvas.height;
        ctx.strokeStyle='#0f0';ctx.lineWidth=2;ctx.strokeRect(x,y,w,h);
        const sc=getScore(det);
        ctx.fillStyle='#0f0';ctx.font='bold 11px Arial';
        ctx.fillText(typeof sc==='number'?(sc*100).toFixed(0)+'%':'?',x,y-4);
      });
    }
    const nowMs=Date.now();
    if(nowMs-lastPostTime>=intervalSec*1000){
      lastPostTime=nowMs;
      if(results.detections&&results.detections.length>0){
        let faceImages=[];
        if(currentBitmap){
          const tc=document.createElement('canvas'),tctx=tc.getContext('2d');
          tc.width=64;tc.height=64;
          results.detections.forEach(d=>{
            const b=getBox(d);if(!b) return;
            tctx.clearRect(0,0,64,64);
            tctx.drawImage(currentBitmap,b.xMin*currentBitmap.width,b.yMin*currentBitmap.height,b.width*currentBitmap.width,b.height*currentBitmap.height,0,0,64,64);
            faceImages.push(tc.toDataURL('image/jpeg',0.3));
          });
        }
        const faces=results.detections.map(d=>{
          const b=getBox(d),sc=getScore(d);
          return{xMin:b?b.xMin:null,yMin:b?b.yMin:null,width:b?b.width:null,height:b?b.height:null,score:typeof sc==='number'?sc:null};
        });
        if(streaming){
          fetch('/face',{method:'POST',headers:{'Content-Type':'application/json'},
            body:JSON.stringify({faces:faces,count:faces.length,time:getTimeObj(),images:faceImages})
          }).catch(()=>{});
        }
      } else {
        if(streaming){
          fetch('/face',{method:'POST',headers:{'Content-Type':'application/json'},
            body:JSON.stringify({faces:[],count:0,time:getTimeObj(),images:[]})
          }).catch(()=>{});
        }
      }
    }
  }catch(e){console.error('Draw error:',e);}
  if(currentBitmap){currentBitmap.close();currentBitmap=null;}
  isProcessing=false;
  if(streaming){setTimeout(captureAndProcess,150);}
}

async function captureAndProcess(){
  if(!streaming||isProcessing) return;
  isProcessing=true;
  try{
    const res=await fetch('/capture?t='+Date.now());
    const blob=await res.blob();
    currentBitmap=await createImageBitmap(blob);
    await faceDetection.send({image:currentBitmap});
  }catch(e){
    console.error('Capture/Process error:',e);
    if(currentBitmap){currentBitmap.close();currentBitmap=null;}
    isProcessing=false;
    if(streaming){setTimeout(captureAndProcess,500);}
  }
}

function start(){streaming=true;lastPostTime=Date.now();captureAndProcess();}
function stop(){
  streaming=false;isProcessing=false;
  if(currentBitmap){currentBitmap.close();currentBitmap=null;}
  ctx.clearRect(0,0,canvas.width,canvas.height);
  stats.textContent='Stopped';
}

async function checkMode(){
  try{const res=await fetch('/api/mode',{cache:'no-store'});if(await res.text()!=='camera') location.reload();}catch(e){}
}
setInterval(checkMode,5000);
initMediaPipe();
</script>
</body>
</html>
)CAMERA_HTML";

static const char INDEX_HTML[] PROGMEM = R"INDEX_HTML(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Web Avatar</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#fff; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:100vh; }
  #face-container { width:320px; height:240px; border:2px solid #e0e0e0; border-radius:16px; background:#fafafa; overflow:hidden; box-shadow: 0 4px 12px rgba(0,0,0,0.06); line-height:0; }
  #face { display:block; width:100%; height:100%; object-fit:cover; }
  #mic-wrap { text-align:center; margin-top:16px; }
  #mic-btn { width:48px; height:48px; cursor:pointer; border-radius:50%; }
  #mic-btn:active { transform: scale(0.92); }
</style>
</head>
<body>
  <div id="face-container"><img id="face" src="" alt="face"></div>
  <div id="mic-wrap"><img id="mic-btn" src="data:image/png;base64,__ICON_MIC__" alt="mic"></div>
<script>
const img = document.getElementById('face'), micBtn = document.getElementById('mic-btn');
let lastIdx = -1, currentBlobUrl = null;
async function updateFace() {
  try {
    const idxRes = await fetch('/api/face', {cache:'no-store'});
    const idx = parseInt(await idxRes.text());
    if(idx !== lastIdx) {
      lastIdx = idx;
      const res = await fetch('/face.gif?t=' + Date.now(), {cache:'no-store'});
      if(!res.ok) throw new Error('HTTP ' + res.status);
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      img.onload = () => { if(currentBlobUrl) URL.revokeObjectURL(currentBlobUrl); currentBlobUrl = url; };
      img.onerror = () => { URL.revokeObjectURL(url); };
      img.src = url;
    }
  } catch(e) { console.error(e); }
}
async function checkMode() {
  try { const res = await fetch('/api/mode', {cache:'no-store'}); if (await res.text() !== 'agent') location.reload(); } catch(e) {}
}
setInterval(checkMode, 5000);
updateFace(); setInterval(updateFace, 800);
micBtn.addEventListener('click', async () => {
  try { await fetch('/api/voice', {method:'POST'}); } catch(e) { console.error(e); }
});
</script>
</body>
</html>
)INDEX_HTML";

#endif // WEB_AVATAR_HTML_H