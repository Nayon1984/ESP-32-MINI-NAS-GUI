/*
  ============================================================
                    MINI NAS V1
       ESP32-S3 WROOM N16R8 + 64GB microSD
  ============================================================

  FEATURES
  ------------------------------------------------------------
  - Home WiFi connection
  - Web GUI
  - Username / Password protection
  - Folder browser
  - Create folder
  - Upload files
  - Download files
  - Delete files/folders
  - Rename files
  - Storage capacity
  - Live storage status
  - Device information
  - Reboot
  - mDNS: http://mini-nas.local

  SD CARD
  ------------------------------------------------------------
  ESP32-S3 WROOM CAM board:
      CLK = GPIO39
      CMD = GPIO38
      D0  = GPIO40

  Uses 1-bit SDMMC mode.

  ============================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD_MMC.h>
#include <FS.h>

// ============================================================
// USER CONFIGURATION
// ============================================================

// ---------- HOME WIFI ----------

const char* WIFI_SSID = "RIDOY";
const char* WIFI_PASSWORD = "2026082026";

// ---------- NAS LOGIN ----------

const char* NAS_USERNAME = "N";
const char* NAS_PASSWORD = "2104";

// ---------- NAS HOSTNAME ----------

const char* NAS_HOSTNAME = "mini-nas";

// ============================================================
// SD CARD PINS
// ============================================================

#define SD_CLK  39
#define SD_CMD  38
#define SD_D0   40

// ============================================================
// SERVER
// ============================================================

WebServer server(80);

// ============================================================
// UPLOAD
// ============================================================

File uploadFile;

bool uploadAuthorized = false;
String uploadPath = "";

// ============================================================
// CURRENT FOLDER
// ============================================================

String currentFolder = "/";

// ============================================================
// HELPERS
// ============================================================

String contentType(String filename)
{
    filename.toLowerCase();

    if (filename.endsWith(".html"))
        return "text/html";

    if (filename.endsWith(".htm"))
        return "text/html";

    if (filename.endsWith(".css"))
        return "text/css";

    if (filename.endsWith(".js"))
        return "application/javascript";

    if (filename.endsWith(".json"))
        return "application/json";

    if (filename.endsWith(".txt"))
        return "text/plain";

    if (filename.endsWith(".jpg"))
        return "image/jpeg";

    if (filename.endsWith(".jpeg"))
        return "image/jpeg";

    if (filename.endsWith(".png"))
        return "image/png";

    if (filename.endsWith(".gif"))
        return "image/gif";

    if (filename.endsWith(".webp"))
        return "image/webp";

    if (filename.endsWith(".mp3"))
        return "audio/mpeg";

    if (filename.endsWith(".wav"))
        return "audio/wav";

    if (filename.endsWith(".mp4"))
        return "video/mp4";

    if (filename.endsWith(".avi"))
        return "video/x-msvideo";

    if (filename.endsWith(".pdf"))
        return "application/pdf";

    if (filename.endsWith(".zip"))
        return "application/zip";

    if (filename.endsWith(".bin"))
        return "application/octet-stream";

    return "application/octet-stream";
}

// ============================================================
// AUTHENTICATION
// ============================================================

bool checkAuth()
{
    if (!server.authenticate(
            NAS_USERNAME,
            NAS_PASSWORD))
    {
        server.requestAuthentication();

        return false;
    }

    return true;
}

// ============================================================
// PATH SECURITY
// ============================================================

bool validPath(String path)
{
    path.trim();

    if (path.length() == 0)
        return false;

    if (!path.startsWith("/"))
        return false;

    // Prevent directory traversal
    if (path.indexOf("..") >= 0)
        return false;

    // Prevent backslash traversal
    if (path.indexOf("\\") >= 0)
        return false;

    return true;
}

// ============================================================
// NORMALIZE PATH
// ============================================================

String normalizePath(String path)
{
    if (path.length() == 0)
        return "/";

    if (!path.startsWith("/"))
        path = "/" + path;

    while (
        path.length() > 1 &&
        path.endsWith("/")
    )
    {
        path.remove(
            path.length() - 1
        );
    }

    return path;
}

// ============================================================
// FILE SIZE FORMAT
// ============================================================

String formatBytes(uint64_t bytes)
{
    if (bytes < 1024)
        return String(bytes) + " B";

    if (bytes < 1024ULL * 1024ULL)
        return String(
            (double)bytes / 1024.0,
            1
        ) + " KB";

    if (bytes < 1024ULL * 1024ULL * 1024ULL)
        return String(
            (double)bytes /
            (1024.0 * 1024.0),
            1
        ) + " MB";

    return String(
        (double)bytes /
        (1024.0 * 1024.0 * 1024.0),
        2
    ) + " GB";
}

// ============================================================
// STORAGE JSON
// ============================================================

String storageJSON()
{
    uint64_t total =
        SD_MMC.totalBytes();

    uint64_t used =
        SD_MMC.usedBytes();

    uint64_t freeSpace = 0;

    if (total > used)
        freeSpace = total - used;

    String json = "{";

    json += "\"total\":";
    json += String(total);

    json += ",";

    json += "\"used\":";
    json += String(used);

    json += ",";

    json += "\"free\":";
    json += String(freeSpace);

    json += ",";

    json += "\"totalText\":\"";
    json += formatBytes(total);
    json += "\"";

    json += ",";

    json += "\"usedText\":\"";
    json += formatBytes(used);
    json += "\"";

    json += ",";

    json += "\"freeText\":\"";
    json += formatBytes(freeSpace);
    json += "\"";

    json += "}";

    return json;
}

// ============================================================
// URL ENCODE
// ============================================================

String jsonEscape(String text)
{
    text.replace(
        "\\",
        "\\\\"
    );

    text.replace(
        "\"",
        "\\\""
    );

    text.replace(
        "\n",
        "\\n"
    );

    text.replace(
        "\r",
        "\\r"
    );

    return text;
}

// ============================================================
// LIST DIRECTORY
// ============================================================

String directoryJSON(String path)
{
    String json = "[";

    File root =
        SD_MMC.open(path);

    if (!root)
        return "[]";

    if (!root.isDirectory())
        return "[]";

    File file =
        root.openNextFile();

    bool first = true;

    while (file)
    {
        String name =
            String(file.name());

        String displayName =
            name;

        int slash =
            displayName.lastIndexOf("/");

        if (slash >= 0)
        {
            displayName =
                displayName.substring(
                    slash + 1
                );
        }

        if (!first)
            json += ",";

        json += "{";

        json += "\"name\":\"";
        json += jsonEscape(
            displayName
        );
        json += "\"";

        json += ",";

        // Build the complete path. Some ESP32-S3 SDMMC builds
        // return only the basename from file.name() inside a folder.
        String fullPath = path;
        if (fullPath != "/")
            fullPath += "/";
        fullPath += displayName;

        json += "\"path\":\"";
        json += jsonEscape(fullPath);
        json += "\"";

        json += ",";

        if (file.isDirectory())
        {
            json += "\"type\":\"folder\"";
            json += ",";
            json += "\"size\":0";
        }
        else
        {
            json += "\"type\":\"file\"";
            json += ",";
            json += "\"size\":";
            json += String(
                file.size()
            );
        }

        json += "}";

        first = false;

        file =
            root.openNextFile();
    }

    root.close();

    json += "]";

    return json;
}

// ============================================================
// ROOT GUI
// ============================================================

void handleRoot()
{
    if (!checkAuth())
        return;

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1020">
<title>MINI NAS • Control Center</title>
<style>
:root{--bg:#070b14;--p:#0e1626;--p2:#111c30;--line:#22304a;--t:#edf4ff;--m:#8190aa;--b:#4b8cff;--g:#18c58a;--a:#f2ad3d;--r:#ef5b68;--c:#39c8ff}
*{box-sizing:border-box}html,body{margin:0;background:radial-gradient(circle at 80% -10%,#172b52 0,#070b14 42%);color:var(--t);font-family:Segoe UI,Arial,sans-serif}body{min-height:100vh}
button,input{font:inherit}button{border:0;color:#fff;cursor:pointer}.top{position:sticky;top:0;z-index:20;display:flex;align-items:center;gap:12px;padding:13px 20px;background:#090e19e8;backdrop-filter:blur(15px);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:10px}.brandIcon{width:40px;height:40px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(135deg,#287cff,#7c4dff);font-size:20px}.title{font-size:18px;font-weight:800}.sub{font-size:11px;color:var(--m)}.topBtns{margin-left:auto;display:flex;gap:7px}
.btn{padding:10px 13px;border-radius:10px;background:#1b2940;font-weight:700}.primary{background:linear-gradient(135deg,#287cff,#4d8dff)}.success{background:linear-gradient(135deg,#0fa66f,#20c992)}.warning{background:linear-gradient(135deg,#b87a16,#e2a32c)}.danger{background:linear-gradient(135deg,#c93f4d,#ef5b68)}.btn:hover{filter:brightness(1.12)}
.wrap{max-width:1180px;margin:auto;padding:20px}.hero{display:flex;justify-content:space-between;align-items:end;margin:3px 0 18px}.hero h1{margin:0 0 5px;font-size:28px}.hero p{margin:0;color:var(--m);font-size:13px}.online{font-size:11px;color:#9deacb;display:flex;align-items:center;gap:7px}.dot{width:8px;height:8px;border-radius:50%;background:var(--g);box-shadow:0 0 12px var(--g)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.card{background:linear-gradient(180deg,#111c30f5,#0c1422f5);border:1px solid var(--line);border-radius:17px;padding:16px;box-shadow:0 15px 45px #0005}.metric{min-height:108px}.mtop{display:flex;justify-content:space-between;color:var(--m);font-size:11px}.mval{font-size:21px;font-weight:800;margin-top:15px}.msmall{font-size:10px;color:var(--m);margin-top:4px}
.columns{display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:14px;margin-top:14px}.head{display:flex;align-items:center;gap:9px;margin-bottom:12px}.head h2{font-size:16px;margin:0}.grow{margin-left:auto}
.storage{display:flex;align-items:center;gap:18px}.donut{width:130px;height:130px;border-radius:50%;display:grid;place-items:center;flex:none;background:conic-gradient(var(--b) 0deg,#1c2940 0deg)}.donut:before{content:"";width:92px;height:92px;border-radius:50%;background:#0d1627}.dtext{position:absolute;text-align:center}.pct{font-size:22px;font-weight:800}.dsub{font-size:10px;color:var(--m)}.sinfo{flex:1}.stitle{font-size:18px;font-weight:800}.smeta{font-size:12px;color:var(--m);margin-top:5px}.bar{height:8px;background:#1d2a40;border-radius:20px;overflow:hidden;margin-top:15px}.bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,#287cff,#39c8ff);transition:.3s}
.path{background:#08101e;border:1px solid #1d2a41;border-radius:10px;padding:11px;color:#8fbaff;font:12px Consolas,monospace;white-space:nowrap;overflow:auto;margin-bottom:9px}.tools{display:flex;gap:7px;flex-wrap:wrap}.searchrow{display:flex;gap:7px;margin:11px 0}.search{flex:1;background:#080f1b;color:#fff;border:1px solid #263652;border-radius:10px;padding:11px;outline:0}.search:focus{border-color:var(--b)}
.drop{border:1px dashed #34517b;border-radius:12px;padding:15px;text-align:center;color:var(--m);background:#4b8cff08;margin-top:11px}.drop.drag{border-color:var(--c);background:#39c8ff12}.drop strong{color:#dce8fb}.file{display:flex;align-items:center;gap:10px;padding:11px 7px;border-top:1px solid #1b2840}.file:hover{background:#121e32;border-radius:9px}.ficon{width:35px;height:35px;display:grid;place-items:center;border-radius:9px;background:#17243a;font-size:17px;flex:none}.finfo{flex:1;min-width:0}.fname{font-size:13px;font-weight:650;word-break:break-all}.fsize{font-size:10px;color:var(--m);margin-top:3px}.acts{display:flex;gap:5px;flex-wrap:wrap}.small{padding:7px 9px;border-radius:8px;font-size:10px;background:#1b2940;color:#fff;border:0}.empty{text-align:center;padding:28px;color:var(--m)}
.side{display:flex;flex-direction:column;gap:14px}.row{display:flex;justify-content:space-between;gap:10px;padding:10px 0;border-bottom:1px solid #1b2840;font-size:12px}.row:last-child{border:0}.row span{color:var(--m)}.pill{display:inline-flex;align-items:center;gap:5px;padding:5px 8px;border-radius:20px;background:#0d2a22;color:#7ce5be;font-size:10px}
.progress{display:none;margin-top:9px}.track{height:7px;background:#1e2b42;border-radius:20px;overflow:hidden}.fill{height:100%;width:0;background:linear-gradient(90deg,#287cff,#39c8ff)}.ptxt{font-size:10px;color:var(--m);margin-top:5px}.modal{display:none;position:fixed;inset:0;z-index:100;background:#000b;align-items:center;justify-content:center;padding:15px}.mbox{width:min(430px,100%);background:#111b2d;border:1px solid #2a3b59;border-radius:17px;padding:18px}.mbox input{width:100%;background:#080f1b;color:#fff;border:1px solid #2b3b58;border-radius:10px;padding:11px;margin:10px 0;outline:0}.toast{position:fixed;right:16px;bottom:16px;z-index:200;background:#152239;border:1px solid #304463;border-radius:11px;padding:11px 14px;font-size:11px;opacity:0;transform:translateY(15px);transition:.2s}.toast.show{opacity:1;transform:none}.foot{text-align:center;color:#52627b;font-size:10px;padding:18px}
@media(max-width:900px){.grid{grid-template-columns:repeat(2,1fr)}.columns{grid-template-columns:1fr}}@media(max-width:600px){.wrap{padding:12px}.top{padding:11px 12px}.sub{display:none}.hero h1{font-size:22px}.grid{grid-template-columns:1fr 1fr}.storage{gap:12px}.donut{width:105px;height:105px}.donut:before{width:75px;height:75px}.file{flex-wrap:wrap;align-items:flex-start}.finfo{min-width:calc(100% - 50px)}.acts{padding-left:45px;width:100%}.topBtns .txt{display:none}}
</style>
</head>
<body>
<header class="top">
 <div class="brand"><div class="brandIcon">💾</div><div><div class="title">MINI NAS</div><div class="sub">ESP32-S3 • Personal File Server</div></div></div>
 <div class="topBtns"><button class="btn" onclick="refreshAll()">🔄 <span class="txt">Refresh</span></button><button class="btn warning" onclick="rebootNAS()">⟳ <span class="txt">Reboot</span></button></div>
</header>
<main class="wrap">
 <div class="hero"><div><h1>Control Center</h1><p>Fast local file management for your ESP32-S3 NAS.</p></div><div class="online"><i class="dot"></i> NAS ONLINE</div></div>

 <div class="grid">
  <div class="card metric"><div class="mtop"><span>STORAGE USED</span><span>💽</span></div><div id="usedMetric" class="mval">—</div><div id="usedSub" class="msmall">Loading...</div></div>
  <div class="card metric"><div class="mtop"><span>FREE SPACE</span><span>🟢</span></div><div id="freeMetric" class="mval">—</div><div class="msmall">Available on SD card</div></div>
  <div class="card metric"><div class="mtop"><span>DEVICE IP</span><span>🌐</span></div><div id="ipMetric" class="mval">—</div><div class="msmall">Local network</div></div>
  <div class="card metric"><div class="mtop"><span>UPTIME</span><span>⏱️</span></div><div id="uptimeMetric" class="mval">—</div><div class="msmall">Since last reboot</div></div>
 </div>

 <div class="columns">
  <section>
   <div class="card">
    <div class="head"><h2>💽 Storage Overview</h2><div class="grow"><span class="pill"><i class="dot"></i> Healthy</span></div></div>
    <div class="storage"><div class="donut"><div class="dtext"><div id="pct" class="pct">0%</div><div class="dsub">used</div></div></div><div class="sinfo"><div id="storageTitle" class="stitle">Loading...</div><div id="storageMeta" class="smeta">Checking SD card...</div><div class="bar"><i id="storageBar"></i></div></div></div>
   </div>

   <div class="card" style="margin-top:14px">
    <div class="head"><h2>📁 File Manager</h2></div>
    <div id="path" class="path">/</div>
    <div class="tools"><button class="btn primary" onclick="goUp()">⬆ Parent</button><button class="btn" onclick="refreshFiles()">🔄 Refresh</button><button class="btn success" onclick="newFolder()">＋ New Folder</button></div>
    <div class="searchrow"><input id="searchBox" class="search" placeholder="Search files and folders..." oninput="renderFiles()"><button class="btn" onclick="clearSearch()">✕</button></div>
    <div id="dropZone" class="drop"><strong>Drop files here to upload</strong><br><span>or choose multiple files</span><br><br><input id="fileInput" type="file" multiple style="display:none" onchange="showSelected()"><button class="btn primary" onclick="$('fileInput').click()">📤 Choose Files</button><div id="selectedText" style="font-size:10px;margin-top:7px"></div></div>
    <div style="margin-top:8px"><button id="uploadBtn" class="btn success" onclick="uploadFiles()" disabled>⬆ Upload Selected</button></div>
    <div id="progress" class="progress"><div class="track"><div id="fill" class="fill"></div></div><div id="ptxt" class="ptxt">Ready</div></div>
    <div class="fileHead" style="font-size:10px;color:var(--m);padding:10px 2px"><span id="count">0 items</span></div>
    <div id="fileList"><div class="empty">Loading files...</div></div>
   </div>
  </section>

  <aside class="side">
   <div class="card"><div class="head"><h2>📡 Device</h2></div>
    <div class="row"><span>Status</span><span class="pill"><i class="dot"></i>Online</span></div>
    <div class="row"><span>IP Address</span><strong id="ip">—</strong></div>
    <div class="row"><span>Hostname</span><strong>mini-nas.local</strong></div>
    <div class="row"><span>Wi‑Fi RSSI</span><strong id="rssi">—</strong></div>
    <div class="row"><span>SD Card</span><strong>64GB</strong></div>
   </div>
   <div class="card"><div class="head"><h2>⚡ Quick Actions</h2></div>
    <button class="btn primary" style="width:100%;margin-bottom:7px" onclick="newFolder()">＋ Create Folder</button>
    <button class="btn" style="width:100%;margin-bottom:7px" onclick="refreshAll()">🔃 Refresh Everything</button>
    <button class="btn warning" style="width:100%" onclick="rebootNAS()">⟳ Restart NAS</button>
   </div>
   <div class="card"><div class="head"><h2>🔐 Access</h2></div>
    <div class="row"><span>Protocol</span><strong>HTTP / LAN</strong></div><div class="row"><span>Login</span><strong>admin</strong></div>
    <div style="font-size:10px;color:var(--m);line-height:1.5">Keep the NAS on a trusted network. Avoid exposing the ESP32 web server directly to the public Internet.</div>
   </div>
  </aside>
 </div>
 <div class="foot">MINI NAS V1 • ESP32-S3 • Local File Server</div>
</main>

<div id="modal" class="modal"><div class="mbox"><h3 id="modalTitle">Rename Item</h3><input id="modalInput" placeholder="Enter name" autocomplete="off"><div style="text-align:right"><button class="btn" onclick="closeModal()">Cancel</button><button class="btn primary" onclick="modalOK()">OK</button></div></div></div>
<div id="toast" class="toast"></div>

<script>
const $=id=>document.getElementById(id);
let currentPath="/",allFiles=[],selectedFiles=[],modalAction=null,modalData=null;
function toast(t){$("toast").innerText=t;$("toast").classList.add("show");clearTimeout(window.tt);window.tt=setTimeout(()=>$("toast").classList.remove("show"),2200)}
function formatBytes(b){if(b<1024)return b+" B";if(b<1048576)return(b/1024).toFixed(1)+" KB";if(b<1073741824)return(b/1048576).toFixed(1)+" MB";return(b/1073741824).toFixed(2)+" GB"}
function esc(t){let d=document.createElement("div");d.textContent=t;return d.innerHTML}
function icon(n,t){if(t==="folder")return"📁";if(/\.(jpg|jpeg|png|gif|webp|bmp)$/i.test(n))return"🖼️";if(/\.(mp3|wav|ogg|flac)$/i.test(n))return"🎵";if(/\.(mp4|avi|mkv|mov|webm)$/i.test(n))return"🎬";if(/\.(zip|rar|7z|tar|gz)$/i.test(n))return"📦";if(/\.pdf$/i.test(n))return"📕";if(/\.(html|css|js|json|py|cpp|h|ino)$/i.test(n))return"💻";return"📄"}
function loadStorage(){fetch("/api/storage").then(r=>r.json()).then(d=>{let p=d.total>0?d.used/d.total*100:0;$("storageBar").style.width=p+"%";$("pct").innerText=p.toFixed(1)+"%";document.querySelector(".donut").style.background=`conic-gradient(var(--b) ${p*3.6}deg,#1c2940 0deg)`;$("storageTitle").innerText=d.usedText+" used of "+d.totalText;$("storageMeta").innerText=d.freeText+" free";$("usedMetric").innerText=d.usedText;$("freeMetric").innerText=d.freeText;$("usedSub").innerText=d.totalText+" total"}).catch(()=>toast("Storage status unavailable"))}
function loadDevice(){fetch("/api/device").then(r=>r.json()).then(d=>{$("ip").innerText=d.ip;$("ipMetric").innerText=d.ip;$("uptimeMetric").innerText=d.uptime;$("rssi").innerText=d.rssi+" dBm"}).catch(()=>{})}
function refreshFiles(){ $("path").innerText=currentPath;$("fileList").innerHTML='<div class="empty">Loading files...</div>';fetch("/api/list?path="+encodeURIComponent(currentPath)).then(r=>r.json()).then(f=>{allFiles=f;renderFiles()}).catch(()=>{$("fileList").innerHTML='<div class="empty">Could not load folder.</div>'})}
function renderFiles(){let q=($("searchBox").value||"").toLowerCase().trim(),fs=allFiles.filter(f=>!q||f.name.toLowerCase().includes(q));$("count").innerText=fs.length+" item"+(fs.length===1?"":"s");if(!fs.length){$("fileList").innerHTML='<div class="empty">'+(q?"No matching files.":"This folder is empty.")+"</div>";return}$("fileList").innerHTML=fs.map(f=>{let sp=JSON.stringify(f.path),folder=f.type==="folder";return`<div class="file" ondblclick='openItem(${sp},${folder})' title="${folder?"Double-click to open folder":"Double-click to open file"}"><div class="ficon">${icon(f.name,f.type)}</div><div class="finfo"><div class="fname">${esc(f.name)}</div><div class="fsize">${folder?"Folder":formatBytes(f.size)}</div></div><div class="acts">${folder?`<button class="small primary" onclick='openFolder(${sp})'>Open</button>`:`<button class="small primary" onclick='openFile(${sp})'>Open</button><button class="small" onclick='downloadFile(${sp})'>Download</button>`}<button class="small warning" onclick='renameItem(${sp})'>Rename</button><button class="small danger" onclick='deleteItem(${sp})'>Delete</button></div></div>`}).join("")}
function openFolder(p){currentPath=p;refreshFiles()}function goUp(){if(currentPath==="/")return;let p=currentPath.substring(0,currentPath.lastIndexOf("/"));currentPath=p||"/";refreshFiles()}function clearSearch(){$("searchBox").value="";renderFiles()}
function showSelected(){selectedFiles=[...$("fileInput").files];$("uploadBtn").disabled=!selectedFiles.length;$("selectedText").innerText=selectedFiles.length?selectedFiles.length+" file(s) selected":""}
function uploadFiles(override=null){let fs=override||selectedFiles;if(!fs.length){toast("Choose files first.");return}let i=0;$("progress").style.display="block";$("uploadBtn").disabled=true;function next(){if(i>=fs.length){$("fill").style.width="100%";$("ptxt").innerText="Upload complete";selectedFiles=[];$("fileInput").value="";$("selectedText").innerText="";setTimeout(()=>$("progress").style.display="none",1200);loadStorage();refreshFiles();toast("Upload complete");return}let f=fs[i++],x=new XMLHttpRequest(),form=new FormData();form.append("file",f);x.open("POST","/upload?path="+encodeURIComponent(currentPath),true);x.upload.onprogress=e=>{if(e.lengthComputable){let one=100/fs.length,total=(i-1)*one+e.loaded/e.total*one;$("fill").style.width=total+"%";$("ptxt").innerText="Uploading "+f.name+" • "+Math.round(e.loaded/e.total*100)+"%"}};x.onload=()=>{if(x.status>=200&&x.status<300)next();else{$("ptxt").innerText="Upload failed: "+f.name;$("uploadBtn").disabled=false;toast("Upload failed")}};x.onerror=()=>{$("ptxt").innerText="Network error";$("uploadBtn").disabled=false;toast("Upload error")};x.send(form)}next()}
function openItem(p,isFolder){if(isFolder)openFolder(p);else openFile(p)}
function openFile(p){window.open("/download?path="+encodeURIComponent(p)+"&inline=1","_blank")}
function downloadFile(p){window.location.href="/download?path="+encodeURIComponent(p)}
function deleteItem(p){if(!confirm("Delete this item?"))return;fetch("/api/delete?path="+encodeURIComponent(p)).then(r=>r.text()).then(t=>{toast(t);loadStorage();refreshFiles()})}
function newFolder(){modalAction="folder";modalData=currentPath;$("modalTitle").innerText="Create New Folder";$("modalInput").value="";$("modal").style.display="flex";setTimeout(()=>$("modalInput").focus(),50)}
function renameItem(p){modalAction="rename";modalData=p;$("modalTitle").innerText="Rename Item";$("modalInput").value="";$("modal").style.display="flex";setTimeout(()=>$("modalInput").focus(),50)}
function modalOK(){let v=$("modalInput").value.trim();if(!v){toast("Enter a name.");return}if(/[\/\\]/.test(v)||v.includes("..")){toast("Invalid name.");return}if(modalAction==="folder")fetch("/api/mkdir?path="+encodeURIComponent((modalData==="/"?"":modalData)+"/"+v)).then(r=>r.text()).then(t=>{closeModal();toast(t);refreshFiles()});else fetch("/api/rename?old="+encodeURIComponent(modalData)+"&new="+encodeURIComponent(v)).then(r=>r.text()).then(t=>{closeModal();toast(t);refreshFiles()})}
function closeModal(){$("modal").style.display="none"}$("modalInput").addEventListener("keydown",e=>{if(e.key==="Enter")modalOK();if(e.key==="Escape")closeModal()});
function rebootNAS(){if(!confirm("Reboot MINI NAS?"))return;fetch("/api/reboot");toast("NAS is rebooting...")}
function refreshAll(){loadStorage();refreshFiles();loadDevice()}
let dz=$("dropZone");["dragenter","dragover"].forEach(e=>dz.addEventListener(e,x=>{x.preventDefault();dz.classList.add("drag")}));["dragleave","drop"].forEach(e=>dz.addEventListener(e,x=>{x.preventDefault();dz.classList.remove("drag")}));dz.addEventListener("drop",e=>{let fs=[...e.dataTransfer.files];if(fs.length)uploadFiles(fs)});
refreshAll();setInterval(loadStorage,3000);setInterval(loadDevice,5000);
</script>
</body>
</html>
)rawliteral";

    server.send(
        200,
        "text/html; charset=utf-8",
        html
    );
}

// ============================================================
// STORAGE API
// ============================================================

void handleStorage()
{
    if (!checkAuth())
        return;

    server.send(
        200,
        "application/json",
        storageJSON()
    );
}

// ============================================================
// DIRECTORY API
// ============================================================

void handleList()
{
    if (!checkAuth())
        return;

    if (!server.hasArg("path"))
    {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );

        return;
    }

    String path =
        server.arg("path");

    path =
        normalizePath(path);

    if (!validPath(path))
    {
        server.send(
            400,
            "text/plain",
            "Invalid path"
        );

        return;
    }

    File dir =
        SD_MMC.open(path);

    if (!dir ||
        !dir.isDirectory())
    {
        server.send(
            404,
            "text/plain",
            "Folder not found"
        );

        return;
    }

    String json =
        directoryJSON(path);

    server.send(
        200,
        "application/json",
        json
    );
}

// ============================================================
// CREATE FOLDER
// ============================================================

void handleMkdir()
{
    if (!checkAuth())
        return;

    if (!server.hasArg("path"))
    {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );

        return;
    }

    String path =
        normalizePath(
            server.arg("path")
        );

    if (!validPath(path))
    {
        server.send(
            400,
            "text/plain",
            "Invalid path"
        );

        return;
    }

    if (SD_MMC.exists(path))
    {
        server.send(
            409,
            "text/plain",
            "Already exists"
        );

        return;
    }

    if (SD_MMC.mkdir(path))
    {
        server.send(
            200,
            "text/plain",
            "Folder created"
        );
    }
    else
    {
        server.send(
            500,
            "text/plain",
            "Create folder failed"
        );
    }
}

// ============================================================
// DELETE
// ============================================================

bool deleteRecursive(
    String path
)
{
    File file =
        SD_MMC.open(path);

    if (!file)
        return false;


    if (!file.isDirectory())
    {
        file.close();

        return SD_MMC.remove(path);
    }


    File child =
        file.openNextFile();


    while (child)
    {
        String childPath =
            String(child.name());

        child.close();

        deleteRecursive(
            childPath
        );

        child =
            file.openNextFile();
    }


    file.close();

    return SD_MMC.rmdir(path);
}


void handleDelete()
{
    if (!checkAuth())
        return;

    if (!server.hasArg("path"))
    {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );

        return;
    }

    String path =
        normalizePath(
            server.arg("path")
        );

    if (
        path == "/" ||
        !validPath(path)
    )
    {
        server.send(
            400,
            "text/plain",
            "Cannot delete root"
        );

        return;
    }


    if (!SD_MMC.exists(path))
    {
        server.send(
            404,
            "text/plain",
            "Not found"
        );

        return;
    }


    if (
        deleteRecursive(path)
    )
    {
        server.send(
            200,
            "text/plain",
            "Deleted successfully"
        );
    }
    else
    {
        server.send(
            500,
            "text/plain",
            "Delete failed"
        );
    }
}

// ============================================================
// RENAME
// ============================================================

void handleRename()
{
    if (!checkAuth())
        return;

    if (
        !server.hasArg("old") ||
        !server.hasArg("new")
    )
    {
        server.send(
            400,
            "text/plain",
            "Missing arguments"
        );

        return;
    }

    String oldPath =
        normalizePath(
            server.arg("old")
        );

    String newName =
        server.arg("new");


    if (
        !validPath(oldPath) ||
        newName.length() == 0 ||
        newName.indexOf("/") >= 0 ||
        newName.indexOf("\\") >= 0 ||
        newName.indexOf("..") >= 0
    )
    {
        server.send(
            400,
            "text/plain",
            "Invalid name"
        );

        return;
    }


    int slash =
        oldPath.lastIndexOf("/");


    String parent =
        oldPath.substring(
            0,
            slash
        );


    if (parent.length() == 0)
        parent = "/";


    String newPath =
        parent;

    if (!newPath.endsWith("/"))
        newPath += "/";

    newPath += newName;


    if (
        SD_MMC.exists(newPath)
    )
    {
        server.send(
            409,
            "text/plain",
            "Destination already exists"
        );

        return;
    }


    if (
        SD_MMC.rename(
            oldPath,
            newPath
        )
    )
    {
        server.send(
            200,
            "text/plain",
            "Renamed successfully"
        );
    }
    else
    {
        server.send(
            500,
            "text/plain",
            "Rename failed"
        );
    }
}

// ============================================================
// DOWNLOAD
// ============================================================

void handleDownload()
{
    if (!checkAuth())
        return;

    if (!server.hasArg("path"))
    {
        server.send(400, "text/plain", "Missing path");
        return;
    }

    String path = normalizePath(server.arg("path"));

    if (!validPath(path))
    {
        server.send(400, "text/plain", "Invalid path");
        return;
    }

    // Open the exact absolute path sent by the File Manager.
    File file = SD_MMC.open(path, FILE_READ);

    if (!file || file.isDirectory())
    {
        if (file)
            file.close();
        server.send(404, "text/plain", "File not found");
        return;
    }

    String downloadName = path.substring(path.lastIndexOf("/") + 1);

    // Double-click opens browser-supported files in a new tab.
    // Normal Download keeps the attachment behavior.
    bool openInline = server.hasArg("inline") && server.arg("inline") == "1";

    if (openInline)
    {
        server.sendHeader(
            "Content-Disposition",
            "inline; filename=\"" + downloadName + "\""
        );
        server.sendHeader("Cache-Control", "no-cache");

        server.streamFile(
            file,
            contentType(downloadName)
        );
    }
    else
    {
        server.sendHeader(
            "Content-Disposition",
            "attachment; filename=\"" + downloadName + "\""
        );
        server.sendHeader("Content-Length", String(file.size()));
        server.sendHeader("Cache-Control", "no-cache");

        server.streamFile(
            file,
            contentType(downloadName)
        );
    }
    file.close();
}

// ============================================================
// UPLOAD CALLBACK
// ============================================================

void handleUpload()
{
    HTTPUpload& upload =
        server.upload();


    if (
        upload.status ==
        UPLOAD_FILE_START
    )
    {
        uploadAuthorized =
            server.authenticate(
                NAS_USERNAME,
                NAS_PASSWORD
            );


        if (!uploadAuthorized)
        {
            Serial.println(
                "Upload authentication failed"
            );

            return;
        }


        String directory =
            "/";


        if (
            server.hasArg("path")
        )
        {
            directory =
                server.arg("path");

            directory =
                normalizePath(
                    directory
                );
        }


        if (!validPath(directory))
        {
            uploadAuthorized = false;

            return;
        }


        String filename =
            upload.filename;


        // Get only filename
        int slash =
            filename.lastIndexOf("/");


        if (slash >= 0)
        {
            filename =
                filename.substring(
                    slash + 1
                );
        }


        // Security
        if (
            filename.indexOf("..") >= 0 ||
            filename.indexOf("/") >= 0 ||
            filename.indexOf("\\") >= 0
        )
        {
            uploadAuthorized = false;

            return;
        }


        uploadPath =
            directory;


        if (!uploadPath.endsWith("/"))
            uploadPath += "/";


        uploadPath += filename;


        Serial.print(
            "UPLOAD START: "
        );

        Serial.println(
            uploadPath
        );


        // Remove old file
        if (
            SD_MMC.exists(
                uploadPath
            )
        )
        {
            SD_MMC.remove(
                uploadPath
            );
        }


        uploadFile =
            SD_MMC.open(
                uploadPath,
                FILE_WRITE
            );
    }


    else if (
        upload.status ==
        UPLOAD_FILE_WRITE
    )
    {
        if (
            uploadAuthorized &&
            uploadFile
        )
        {
            uploadFile.write(
                upload.buf,
                upload.currentSize
            );
        }
    }


    else if (
        upload.status ==
        UPLOAD_FILE_END
    )
    {
        if (uploadFile)
        {
            uploadFile.close();
        }


        Serial.print(
            "UPLOAD END: "
        );

        Serial.print(
            uploadPath
        );

        Serial.print(
            " | "
        );

        Serial.print(
            upload.totalSize
        );

        Serial.println(
            " bytes"
        );
    }


    else if (
        upload.status ==
        UPLOAD_FILE_ABORTED
    )
    {
        if (uploadFile)
            uploadFile.close();


        Serial.println(
            "UPLOAD ABORTED"
        );
    }
}

// ============================================================
// UPLOAD COMPLETE HANDLER
// ============================================================

void handleUploadComplete()
{
    if (!checkAuth())
        return;


    if (!uploadAuthorized)
    {
        server.send(
            401,
            "text/plain",
            "Unauthorized"
        );

        return;
    }


    server.send(
        200,
        "text/plain",
        "Upload complete"
    );


    uploadAuthorized =
        false;
}

// ============================================================
// DEVICE INFO
// ============================================================

String uptimeString()
{
    unsigned long seconds =
        millis() / 1000UL;

    unsigned long days =
        seconds / 86400UL;

    seconds %= 86400UL;

    unsigned long hours =
        seconds / 3600UL;

    seconds %= 3600UL;

    unsigned long minutes =
        seconds / 60UL;

    seconds %= 60UL;


    char buffer[64];

    sprintf(
        buffer,
        "%lu d %02lu:%02lu:%02lu",
        days,
        hours,
        minutes,
        seconds
    );


    return String(buffer);
}


void handleDevice()
{
    if (!checkAuth())
        return;


    String json = "{";


    json += "\"ip\":\"";
    json +=
        WiFi.localIP().toString();
    json += "\"";


    json += ",";


    json += "\"ssid\":\"";
    json += WIFI_SSID;
    json += "\"";


    json += ",";


    json += "\"hostname\":\"";
    json += NAS_HOSTNAME;
    json += "\"";


    json += ",";


    json += "\"uptime\":\"";
    json += uptimeString();
    json += "\"";


    json += ",";


    json += "\"rssi\":";
    json +=
        String(
            WiFi.RSSI()
        );


    json += "}";


    server.send(
        200,
        "application/json",
        json
    );
}

// ============================================================
// REBOOT
// ============================================================

void handleReboot()
{
    if (!checkAuth())
        return;


    server.send(
        200,
        "text/plain",
        "Rebooting..."
    );


    delay(500);


    ESP.restart();
}

// ============================================================
// 404
// ============================================================

void handleNotFound()
{
    if (!checkAuth())
        return;


    server.send(
        404,
        "text/plain",
        "404 - Not Found"
    );
}

// ============================================================
// SD INITIALIZATION
// ============================================================

bool initSD()
{
    Serial.println();
    Serial.println(
        "Initializing SD card..."
    );


    // IMPORTANT:
    // This board uses 1-bit SDMMC.
    //
    // CLK = GPIO39
    // CMD = GPIO38
    // D0  = GPIO40

    if (
        !SD_MMC.setPins(
            SD_CLK,
            SD_CMD,
            SD_D0
        )
    )
    {
        Serial.println(
            "SD_MMC pin configuration failed!"
        );

        return false;
    }


    if (
        !SD_MMC.begin(
            "/sdcard",
            true
        )
    )
    {
        Serial.println(
            "SD Card mount failed!"
        );

        return false;
    }


    uint8_t cardType =
        SD_MMC.cardType();


    if (
        cardType == CARD_NONE
    )
    {
        Serial.println(
            "No SD card detected!"
        );

        return false;
    }


    Serial.println(
        "SD CARD READY"
    );


    Serial.print(
        "Total: "
    );

    Serial.print(
        formatBytes(
            SD_MMC.totalBytes()
        )
    );

    Serial.println();


    Serial.print(
        "Used: "
    );

    Serial.print(
        formatBytes(
            SD_MMC.usedBytes()
        )
    );

    Serial.println();


    Serial.print(
        "Free: "
    );

    Serial.print(
        formatBytes(
            SD_MMC.totalBytes()
            -
            SD_MMC.usedBytes()
        )
    );

    Serial.println();


    return true;
}

// ============================================================
// WIFI
// ============================================================

bool connectWiFi()
{
    Serial.println();
    Serial.print(
        "Connecting to WiFi: "
    );

    Serial.println(
        WIFI_SSID
    );


    WiFi.mode(
        WIFI_STA
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    unsigned long start =
        millis();


    while (
        WiFi.status() != WL_CONNECTED
        &&
        millis() - start < 30000
    )
    {
        delay(500);

        Serial.print(".");
    }


    Serial.println();


    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println(
            "WiFi connection FAILED!"
        );

        return false;
    }


    Serial.println(
        "WiFi connected!"
    );


    Serial.print(
        "IP address: "
    );

    Serial.println(
        WiFi.localIP()
    );


    Serial.print(
        "RSSI: "
    );

    Serial.println(
        WiFi.RSSI()
    );


    return true;
}

// ============================================================
// WEB SERVER ROUTES
// ============================================================

void setupRoutes()
{
    // Main GUI
    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );


    // APIs
    server.on(
        "/api/storage",
        HTTP_GET,
        handleStorage
    );


    server.on(
        "/api/list",
        HTTP_GET,
        handleList
    );


    server.on(
        "/api/mkdir",
        HTTP_GET,
        handleMkdir
    );


    server.on(
        "/api/delete",
        HTTP_GET,
        handleDelete
    );


    server.on(
        "/api/rename",
        HTTP_GET,
        handleRename
    );


    server.on(
        "/api/device",
        HTTP_GET,
        handleDevice
    );


    server.on(
        "/api/reboot",
        HTTP_GET,
        handleReboot
    );


    // Download
    server.on(
        "/download",
        HTTP_GET,
        handleDownload
    );


    // Upload
    server.on(
        "/upload",
        HTTP_POST,
        handleUploadComplete,
        handleUpload
    );


    // 404
    server.onNotFound(
        handleNotFound
    );
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();
    Serial.println(
        "======================================"
    );

    Serial.println(
        "          MINI NAS V1"
    );

    Serial.println(
        "     ESP32-S3 + 64GB SD CARD"
    );

    Serial.println(
        "======================================"
    );


    // ---------------- SD ----------------

    if (!initSD())
    {
        Serial.println();
        Serial.println(
            "WARNING:"
        );

        Serial.println(
            "SD card initialization failed."
        );

        Serial.println(
            "NAS cannot operate without SD."
        );


        while (true)
        {
            delay(1000);
        }
    }


    // ---------------- WiFi ----------------

    if (!connectWiFi())
    {
        Serial.println();
        Serial.println(
            "WiFi FAILED."
        );

        Serial.println(
            "Check SSID/password."
        );


        while (true)
        {
            delay(1000);
        }
    }


    // ---------------- mDNS ----------------

    if (
        MDNS.begin(
            NAS_HOSTNAME
        )
    )
    {
        Serial.println(
            "mDNS started!"
        );

        Serial.print(
            "Open: http://"
        );

        Serial.print(
            NAS_HOSTNAME
        );

        Serial.println(
            ".local"
        );
    }
    else
    {
        Serial.println(
            "mDNS failed."
        );
    }


    // ---------------- Server ----------------

    setupRoutes();


    server.begin();


    Serial.println();
    Serial.println(
        "======================================"
    );

    Serial.println(
        "          MINI NAS READY"
    );

    Serial.println(
        "======================================"
    );


    Serial.print(
        "IP: http://"
    );

    Serial.println(
        WiFi.localIP()
    );


    Serial.print(
        "mDNS: http://"
    );

    Serial.print(
        NAS_HOSTNAME
    );

    Serial.println(
        ".local"
    );


    Serial.println();
    Serial.println(
        "Username: admin"
    );

    Serial.println(
        "Password: admin123"
    );


    Serial.println();
    Serial.println(
        "Waiting for clients..."
    );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    server.handleClient();

    delay(2);
}