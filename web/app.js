const state = {
  token: localStorage.getItem("cloud_disk_token") || "",
  username: localStorage.getItem("cloud_disk_username") || "",
  currentFolderId: 0,
  currentView: "files",
  folderStack: [{ id: 0, name: "根目录" }],
  showcaseMode: location.hostname.endsWith("github.io"),
  demoNextId: Number(localStorage.getItem("cloud_disk_demo_next_id") || "3"),
  demoFiles: JSON.parse(localStorage.getItem("cloud_disk_demo_files") || "[]"),
  demoRecycle: JSON.parse(localStorage.getItem("cloud_disk_demo_recycle") || "[]"),
};

const $ = (id) => document.getElementById(id);

function apiUrl(path) {
  return path;
}

function saveDemoState() {
  localStorage.setItem("cloud_disk_demo_files", JSON.stringify(state.demoFiles));
  localStorage.setItem("cloud_disk_demo_recycle", JSON.stringify(state.demoRecycle));
  localStorage.setItem("cloud_disk_demo_next_id", String(state.demoNextId));
}

function setStatus(message, isError = false) {
  $("status-text").textContent = message;
  $("status-card").style.borderColor = isError ? "rgb(180 35 24 / 0.35)" : "";
}

function showScreen(name) {
  $("auth-screen").classList.toggle("hidden", name !== "auth");
  $("app-shell").classList.toggle("hidden", name !== "app");
}

function authHeaders(extra = {}) {
  return state.token ? { ...extra, Authorization: `Bearer ${state.token}` } : extra;
}

async function fetchJson(path, options = {}) {
  const response = await fetch(apiUrl(path), options);
  const text = await response.text();
  let doc;
  try {
    doc = JSON.parse(text);
  } catch {
    throw new Error(text || `请求失败：${response.status}`);
  }
  if (!response.ok || doc.code !== 0) {
    throw new Error(doc.message || `请求失败：${response.status}`);
  }
  return doc.data;
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(1)} GB`;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function contentHash(bytes) {
  const mask = (1n << 64n) - 1n;
  let hash = 1469598103934665603n;
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * 1099511628211n) & mask;
  }

  const parts = [];
  for (let i = 0n; i < 4n; i += 1n) {
    let mixed = (hash + 0x9e3779b97f4a7c15n * (i + 1n)) & mask;
    mixed ^= mixed >> 30n;
    mixed = (mixed * 0xbf58476d1ce4e5b9n) & mask;
    mixed ^= mixed >> 27n;
    mixed = (mixed * 0x94d049bb133111ebn) & mask;
    mixed ^= mixed >> 31n;
    parts.push(mixed.toString(16).padStart(16, "0"));
  }
  return parts.join("");
}

function setUploadProgress(percent) {
  $("upload-progress").classList.remove("hidden");
  $("upload-progress-bar").style.width = `${Math.max(0, Math.min(100, percent))}%`;
}

function hideUploadProgress() {
  $("upload-progress").classList.add("hidden");
  $("upload-progress-bar").style.width = "0%";
}

function updateModeUi() {
  $("mode-pill").textContent = state.showcaseMode
    ? "GitHub Pages 演示模式"
    : "后端连接模式";
  $("sidebar-subtitle").textContent = state.showcaseMode ? "前端演示工作台" : "我的文件工作台";
}

function enterWorkspace(message) {
  showScreen("app");
  updateAccountUi();
  updateBreadcrumb();
  switchView("files");
  setStatus(message);
  loadFiles().catch(showError);
  loadRecycle().catch(showError);
}

function updateAccountUi() {
  const modeText = state.showcaseMode ? "演示账号" : "本地会话已连接";
  const used = state.showcaseMode
    ? state.demoFiles.filter((file) => file.is_dir !== "true").reduce((sum, file) => sum + Number(file.size_bytes || 0), 0)
    : null;
  $("account-card").innerHTML = `
    <strong>${escapeHtml(state.username || "未登录")}</strong>
    <span>${state.showcaseMode ? `已用空间：${formatBytes(used)}` : modeText}</span>
  `;
}

function updateBreadcrumb() {
  $("breadcrumb").textContent = `当前位置：${state.folderStack.map((item) => item.name).join(" / ")}`;
}

function switchView(view) {
  state.currentView = view;
  const titles = {
    files: "全部文件",
    recycle: "回收站",
    share: "分享管理",
    advanced: "高级能力",
  };
  $("page-title").textContent = titles[view] || "全部文件";
  $("files-actions").classList.toggle("hidden", view !== "files");
  ["files", "recycle", "share", "advanced"].forEach((name) => {
    $(`${name}-view`).classList.toggle("hidden", name !== view);
  });
  document.querySelectorAll(".nav-item").forEach((item) => {
    item.classList.toggle("active", item.dataset.view === view);
  });
  if (view === "recycle") {
    loadRecycle().catch(showError);
  }
}

async function register() {
  const username = $("username").value.trim();
  const password = $("password").value;
  if (!username || password.length < 6) {
    throw new Error("请输入用户名和至少 6 位密码。");
  }

  if (state.showcaseMode) {
    state.token = `demo-${Date.now()}`;
    state.username = username;
    localStorage.setItem("cloud_disk_token", state.token);
    localStorage.setItem("cloud_disk_username", state.username);
    seedDemoFiles();
    enterWorkspace("演示账号已创建。你现在可以直接体验网盘流程。");
    return;
  }

  await fetchJson("/api/user/register", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  $("auth-hint").textContent = "注册成功。现在可以登录。";
}

async function login() {
  const username = $("username").value.trim();
  const password = $("password").value;
  if (!username || password.length < 6) {
    throw new Error("请输入用户名和至少 6 位密码。");
  }

  if (state.showcaseMode) {
    state.token = `demo-${Date.now()}`;
    state.username = username;
    localStorage.setItem("cloud_disk_token", state.token);
    localStorage.setItem("cloud_disk_username", state.username);
    seedDemoFiles();
    enterWorkspace("已进入 GitHub Pages 演示模式。这里的数据保存在浏览器本地。");
    return;
  }

  const data = await fetchJson("/api/user/login", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  state.token = data.token;
  state.username = username;
  localStorage.setItem("cloud_disk_token", state.token);
  localStorage.setItem("cloud_disk_username", state.username);
  await loadProfile();
  enterWorkspace("登录成功。你可以开始上传和管理文件。");
}

function logout() {
  state.token = "";
  state.username = "";
  state.currentFolderId = 0;
  state.currentView = "files";
  state.folderStack = [{ id: 0, name: "根目录" }];
  localStorage.removeItem("cloud_disk_token");
  localStorage.removeItem("cloud_disk_username");
  $("password").value = "";
  showScreen("auth");
  $("auth-hint").textContent = state.showcaseMode
    ? "这是 GitHub Pages 演示模式，输入任意账号即可体验。"
    : "没有账号可以直接注册。";
}

function seedDemoFiles() {
  if (state.demoFiles.length > 0) return;
  state.demoFiles = [
    {
      id: "1",
      parent_id: "0",
      name: "作品材料",
      is_dir: "true",
      is_deleted: "false",
      size_bytes: "0",
    },
    {
      id: "2",
      parent_id: "0",
      name: "项目说明.txt",
      is_dir: "false",
      is_deleted: "false",
      size_bytes: "52",
      content: "这是 GitHub Pages 演示文件。本机后端版支持真实持久化。",
      sha256: "demo-readme",
    },
  ];
  state.demoRecycle = [];
  state.demoNextId = 3;
  saveDemoState();
}

async function loadProfile() {
  if (!state.token || state.showcaseMode) {
    updateAccountUi();
    return;
  }
  const profile = await fetchJson("/api/user/me", { headers: authHeaders() });
  $("account-card").innerHTML = `
    <strong>${escapeHtml(profile.username)}</strong>
    <span>已用空间：${formatBytes(profile.storage_used)}</span>
  `;
}

async function createFolder() {
  const name = $("folder-name").value.trim();
  if (!name) throw new Error("请输入文件夹名称。");

  if (state.showcaseMode) {
    state.demoFiles.push({
      id: String(state.demoNextId++),
      parent_id: String(state.currentFolderId),
      name,
      is_dir: "true",
      is_deleted: "false",
      size_bytes: "0",
    });
    saveDemoState();
  } else {
    await fetchJson("/api/folders", {
      method: "POST",
      headers: authHeaders({ "Content-Type": "application/json" }),
      body: JSON.stringify({ parent_id: String(state.currentFolderId), name }),
    });
  }

  $("folder-name").value = "";
  setStatus("文件夹创建成功。");
  await loadFiles();
}

async function loadFiles() {
  if (!state.token) return;
  updateBreadcrumb();
  if (state.showcaseMode) {
    renderFiles(state.demoFiles.filter((file) => file.parent_id === String(state.currentFolderId)));
    updateAccountUi();
    return;
  }
  const files = await fetchJson(`/api/files?parent_id=${state.currentFolderId}`, { headers: authHeaders() });
  renderFiles(files);
}

function renderFiles(files) {
  const table = $("file-table");
  if (!files.length) {
    table.innerHTML = '<div class="empty-state">当前目录还是空的。可以先创建文件夹或上传文件。</div>';
    return;
  }

  table.innerHTML = files
    .map((file) => {
      const isDir = file.is_dir === "true";
      const type = isDir ? "夹" : "文";
      const openButton = isDir
        ? `<button class="secondary" data-open-folder="${file.id}" data-folder-name="${escapeHtml(file.name)}">打开</button>`
        : `<button class="secondary" data-download="${file.id}">下载</button>`;
      const shareButton = isDir ? "" : `<button class="secondary" data-share="${file.id}">分享</button>`;
      return `
        <div class="file-row">
          <div class="file-main">
            <div class="file-title">
              <span class="file-type">${type}</span>
              <span class="file-name">${escapeHtml(file.name)}</span>
            </div>
            <div class="meta">大小：${formatBytes(file.size_bytes)}，文件 ID：${file.id}</div>
          </div>
          <div class="row-actions">
            ${openButton}
            ${shareButton}
            <button class="danger" data-delete="${file.id}">删除</button>
          </div>
        </div>
      `;
    })
    .join("");
}

async function uploadSelectedFile() {
  const input = $("file-input");
  const file = input.files[0];
  if (!file) throw new Error("请先选择文件。");
  if ($("chunk-mode").checked) {
    await uploadChunked(file);
  } else {
    await uploadDirect(file);
  }
  input.value = "";
  await Promise.all([loadFiles(), loadProfile()]);
}

async function uploadDirect(file) {
  setUploadProgress(35);
  if (state.showcaseMode) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const hash = contentHash(bytes);
    const text = await file.text().catch(() => "");
    state.demoFiles.push({
      id: String(state.demoNextId++),
      parent_id: String(state.currentFolderId),
      name: file.name,
      is_dir: "false",
      is_deleted: "false",
      size_bytes: String(file.size),
      content: text || `演示文件：${file.name}`,
      sha256: hash,
    });
    saveDemoState();
    fillInstantFields(file.name, hash, file.size);
    setUploadProgress(100);
    setStatus(`演示上传完成：${file.name}。`);
    setTimeout(hideUploadProgress, 500);
    return;
  }

  const response = await fetch(apiUrl(`/api/files/upload?parent_id=${state.currentFolderId}&name=${encodeURIComponent(file.name)}`), {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/octet-stream" }),
    body: file,
  });
  const doc = await response.json();
  if (!response.ok || doc.code !== 0) throw new Error(doc.message || "上传失败。");
  fillInstantFields(file.name, doc.data.sha256 || "", doc.data.size_bytes || file.size);
  setUploadProgress(100);
  setStatus(`上传完成：${file.name}。哈希已填入秒传区域。`);
  setTimeout(hideUploadProgress, 500);
}

async function uploadChunked(file) {
  if (state.showcaseMode) {
    setUploadProgress(25);
    await new Promise((resolve) => setTimeout(resolve, 120));
    setUploadProgress(70);
    await uploadDirect(file);
    setStatus(`演示分片上传完成：${file.name}。`);
    return;
  }

  const bytes = new Uint8Array(await file.arrayBuffer());
  const hash = contentHash(bytes);
  const chunkSize = 1024 * 1024;
  const totalChunks = Math.ceil(file.size / chunkSize);

  const session = await fetchJson("/api/uploads/init", {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({
      parent_id: String(state.currentFolderId),
      name: file.name,
      sha256: hash,
      size_bytes: String(file.size),
      chunk_size: String(chunkSize),
      total_chunks: String(totalChunks),
    }),
  });

  if (session.status === "instant_available") {
    await fetchJson("/api/files/instant", {
      method: "POST",
      headers: authHeaders({ "Content-Type": "application/json" }),
      body: JSON.stringify({
        parent_id: String(state.currentFolderId),
        name: file.name,
        sha256: hash,
        size_bytes: String(file.size),
      }),
    });
    fillInstantFields(file.name, hash, file.size);
    setUploadProgress(100);
    setStatus(`服务端已有相同内容，已秒传：${file.name}。`);
    setTimeout(hideUploadProgress, 500);
    return;
  }

  const uploaded = new Set((session.uploaded_chunks || []).map((item) => Number(item)));
  const status = await fetchJson(`/api/uploads/status?upload_id=${session.upload_id}`, {
    headers: authHeaders(),
  });
  (status.uploaded_chunks || []).forEach((item) => uploaded.add(Number(item)));

  for (let index = 0; index < totalChunks; index += 1) {
    if (uploaded.has(index)) {
      setUploadProgress(Math.round(((index + 1) / totalChunks) * 85));
      continue;
    }
    const start = index * chunkSize;
    const end = Math.min(file.size, start + chunkSize);
    await fetch(apiUrl(`/api/uploads/chunk?upload_id=${session.upload_id}&chunk_index=${index}`), {
      method: "POST",
      headers: authHeaders({ "Content-Type": "application/octet-stream" }),
      body: file.slice(start, end),
    });
    setUploadProgress(Math.round(((index + 1) / totalChunks) * 85));
  }

  const completed = await fetchJson(`/api/uploads/complete?upload_id=${session.upload_id}`, {
    method: "POST",
    headers: authHeaders(),
  });
  fillInstantFields(file.name, completed.sha256 || hash, completed.size_bytes || file.size);
  setUploadProgress(100);
  setStatus(`分片上传完成：${file.name}${uploaded.size ? "（含断点续传）" : ""}。`);
  setTimeout(hideUploadProgress, 500);
}

function fillInstantFields(name, hash, size) {
  $("instant-hash").value = hash;
  $("instant-size").value = String(size);
  $("instant-name").value = `copy-${name}`;
}

async function instantUpload() {
  const name = $("instant-name").value.trim();
  const sha256 = $("instant-hash").value.trim();
  const sizeBytes = $("instant-size").value.trim();
  if (!name || !sha256 || !sizeBytes) throw new Error("请填写秒传文件名、哈希和大小。");

  if (state.showcaseMode) {
    const source = state.demoFiles.find((file) => file.sha256 === sha256 && file.is_dir !== "true");
    if (!source) throw new Error("演示模式下没有找到相同哈希的文件，请先上传一次。");
    state.demoFiles.push({
      ...source,
      id: String(state.demoNextId++),
      parent_id: String(state.currentFolderId),
      name,
    });
    saveDemoState();
  } else {
    await fetchJson("/api/files/instant", {
      method: "POST",
      headers: authHeaders({ "Content-Type": "application/json" }),
      body: JSON.stringify({
        parent_id: String(state.currentFolderId),
        name,
        sha256,
        size_bytes: sizeBytes,
      }),
    });
  }

  setStatus("秒传文件创建成功。");
  await Promise.all([loadFiles(), loadProfile()]);
}

async function downloadFile(id) {
  if (state.showcaseMode) {
    const file = state.demoFiles.find((item) => item.id === String(id));
    if (!file) throw new Error("文件不存在。");
    downloadText(file.name, file.content || "");
    setStatus(`演示下载已开始：${file.name}`);
    return;
  }

  const response = await fetch(apiUrl(`/api/files/download?id=${id}`), { headers: authHeaders() });
  if (!response.ok) throw new Error("下载失败。");
  const blob = await response.blob();
  const disposition = response.headers.get("Content-Disposition") || "";
  const match = disposition.match(/filename="([^"]+)"/);
  const filename = match ? match[1] : `file-${id}`;
  downloadBlob(filename, blob);
  setStatus(`已开始下载：${filename}`);
}

function downloadText(filename, text) {
  downloadBlob(filename, new Blob([text], { type: "text/plain;charset=utf-8" }));
}

function downloadBlob(filename, blob) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  link.click();
  URL.revokeObjectURL(url);
}

async function deleteFile(id) {
  if (state.showcaseMode) {
    const index = state.demoFiles.findIndex((file) => file.id === String(id));
    if (index < 0) throw new Error("文件不存在。");
    const [removed] = state.demoFiles.splice(index, 1);
    state.demoRecycle.push({ ...removed, is_deleted: "true" });
    saveDemoState();
  } else {
    await fetchJson(`/api/files?id=${id}`, { method: "DELETE", headers: authHeaders() });
  }
  setStatus("文件已移入回收站。");
  await Promise.all([loadFiles(), loadRecycle(), loadProfile()]);
}

async function loadRecycle() {
  if (!state.token) return;
  if (state.showcaseMode) {
    renderRecycle(state.demoRecycle);
    return;
  }
  const files = await fetchJson("/api/recycle", { headers: authHeaders() });
  renderRecycle(files);
}

function renderRecycle(files) {
  const list = $("recycle-list");
  if (!files.length) {
    list.innerHTML = '<div class="empty-state">回收站是空的。</div>';
    return;
  }
  list.innerHTML = files
    .map((file) => `
      <div class="recycle-row">
        <div class="recycle-main">
          <strong>${escapeHtml(file.name)}</strong>
          <span class="meta">大小：${formatBytes(file.size_bytes)}，文件 ID：${file.id}</span>
        </div>
        <div class="row-actions">
          <button class="secondary" data-restore="${file.id}">恢复</button>
          <button class="danger" data-permanent="${file.id}">永久删除</button>
        </div>
      </div>
    `)
    .join("");
}

async function restoreFile(id) {
  if (state.showcaseMode) {
    const index = state.demoRecycle.findIndex((file) => file.id === String(id));
    if (index < 0) throw new Error("回收站里没有这个文件。");
    const [file] = state.demoRecycle.splice(index, 1);
    state.demoFiles.push({ ...file, is_deleted: "false" });
    saveDemoState();
  } else {
    await fetchJson(`/api/recycle/restore?id=${id}`, { method: "POST", headers: authHeaders() });
  }
  setStatus("文件已恢复。");
  await Promise.all([loadFiles(), loadRecycle(), loadProfile()]);
}

async function permanentDelete(id) {
  if (state.showcaseMode) {
    state.demoRecycle = state.demoRecycle.filter((file) => file.id !== String(id));
    saveDemoState();
  } else {
    await fetchJson(`/api/recycle/permanent?id=${id}`, { method: "DELETE", headers: authHeaders() });
  }
  setStatus("文件已永久删除。");
  await Promise.all([loadRecycle(), loadProfile()]);
}

async function shareFile(id) {
  const accessCode = $("share-code").value.trim();
  if (state.showcaseMode) {
    const url = `${location.origin}${location.pathname}?share=${id}${accessCode ? `&code=${encodeURIComponent(accessCode)}` : ""}`;
    $("share-output").value = url;
    switchView("share");
    setStatus("演示分享链接已生成。");
    return;
  }

  const data = await fetchJson("/api/shares", {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({
      file_id: String(id),
      access_code: accessCode,
      allow_download: "true",
    }),
  });
  const base = location.origin;
  const shareUrl = data.url.startsWith("http") ? data.url : `${base}${data.url}`;
  const separator = shareUrl.includes("?") ? "&" : "?";
  const url = accessCode ? `${shareUrl}${separator}code=${encodeURIComponent(accessCode)}` : shareUrl;
  $("share-output").value = url;
  switchView("share");
  try {
    await navigator.clipboard.writeText(url);
    setStatus("分享链接已生成，并已复制到剪贴板。");
  } catch {
    setStatus("分享链接已生成。你可以手动复制。");
  }
}

function openFolder(id, name) {
  state.currentFolderId = Number(id);
  state.folderStack.push({ id: state.currentFolderId, name });
  loadFiles().catch(showError);
}

function backFolder() {
  if (state.folderStack.length <= 1) return;
  state.folderStack.pop();
  state.currentFolderId = state.folderStack[state.folderStack.length - 1].id;
  loadFiles().catch(showError);
}

function showError(error) {
  const message = error.message || String(error);
  const target = $("app-shell").classList.contains("hidden") ? $("auth-hint") : null;
  if (target) {
    target.textContent = message;
  } else {
    setStatus(message, true);
  }
}

function bindEvents() {
  $("register-btn").addEventListener("click", () => register().catch(showError));
  $("login-btn").addEventListener("click", () => login().catch(showError));
  $("logout-btn").addEventListener("click", logout);
  $("create-folder-btn").addEventListener("click", () => createFolder().catch(showError));
  $("upload-btn").addEventListener("click", () => uploadSelectedFile().catch(showError));
  $("instant-btn").addEventListener("click", () => instantUpload().catch(showError));
  $("refresh-btn").addEventListener("click", () => loadFiles().catch(showError));
  $("refresh-recycle-btn").addEventListener("click", () => loadRecycle().catch(showError));
  $("back-btn").addEventListener("click", backFolder);

  document.querySelectorAll(".nav-item").forEach((item) => {
    item.addEventListener("click", () => switchView(item.dataset.view || "files"));
  });

  document.body.addEventListener("click", (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;
    const openId = target.dataset.openFolder;
    const downloadId = target.dataset.download;
    const deleteId = target.dataset.delete;
    const restoreId = target.dataset.restore;
    const permanentId = target.dataset.permanent;
    const shareId = target.dataset.share;

    if (openId) openFolder(openId, target.dataset.folderName || "文件夹");
    if (downloadId) downloadFile(downloadId).catch(showError);
    if (deleteId) deleteFile(deleteId).catch(showError);
    if (restoreId) restoreFile(restoreId).catch(showError);
    if (permanentId) permanentDelete(permanentId).catch(showError);
    if (shareId) shareFile(shareId).catch(showError);
  });
}

async function init() {
  bindEvents();
  updateModeUi();
  if (state.showcaseMode) {
    $("auth-hint").textContent = "这是 GitHub Pages 演示模式，输入任意账号即可体验。";
  }

  if (state.token && state.username) {
    seedDemoFiles();
    enterWorkspace(state.showcaseMode ? "已恢复演示账号。" : "已恢复上次登录状态。");
    if (!state.showcaseMode) {
      await loadProfile().catch(() => logout());
    }
  } else {
    showScreen("auth");
  }
}

init();

