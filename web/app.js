const state = {
  token: localStorage.getItem("cloud_disk_token") || "",
  username: localStorage.getItem("cloud_disk_username") || "",
  currentFolderId: 0,
  folderStack: [{ id: 0, name: "根目录" }],
  showcaseMode: location.hostname.endsWith("github.io"),
};

const $ = (id) => document.getElementById(id);

function setStatus(message, isError = false) {
  $("status-text").textContent = message;
  $("status-card").style.borderColor = isError ? "rgb(180 35 24 / 0.35)" : "";
}

function authHeaders(extra = {}) {
  return state.token ? { ...extra, Authorization: `Bearer ${state.token}` } : extra;
}

async function fetchJson(path, options = {}) {
  const response = await fetch(path, options);
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

function setUploadProgress(percent) {
  $("upload-progress").classList.remove("hidden");
  $("upload-progress-bar").style.width = `${Math.max(0, Math.min(100, percent))}%`;
}

function hideUploadProgress() {
  $("upload-progress").classList.add("hidden");
  $("upload-progress-bar").style.width = "0%";
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

function updateAuthUi() {
  const loggedIn = Boolean(state.token);
  $("logout-btn").classList.toggle("hidden", !loggedIn);
  $("auth-hint").textContent = loggedIn ? `已登录：${state.username}` : "先注册或登录，然后开始管理文件。";
  $("account-card").innerHTML = loggedIn
    ? `<strong>${escapeHtml(state.username)}</strong><span>本地会话已连接</span>`
    : "<span>未登录</span>";
}

function updateBreadcrumb() {
  $("breadcrumb").textContent = `当前位置：${state.folderStack.map((item) => item.name).join(" / ")}`;
}

async function register() {
  const username = $("username").value.trim();
  const password = $("password").value;
  await fetchJson("/api/user/register", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  setStatus("注册成功。现在可以直接登录。");
}

async function login() {
  const username = $("username").value.trim();
  const password = $("password").value;
  const data = await fetchJson("/api/user/login", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  state.token = data.token;
  state.username = username;
  localStorage.setItem("cloud_disk_token", state.token);
  localStorage.setItem("cloud_disk_username", state.username);
  updateAuthUi();
  setStatus("登录成功。你可以开始上传和管理文件。");
  await Promise.all([loadProfile(), loadFiles(), loadRecycle()]);
}

function logout() {
  state.token = "";
  state.username = "";
  state.currentFolderId = 0;
  state.folderStack = [{ id: 0, name: "根目录" }];
  localStorage.removeItem("cloud_disk_token");
  localStorage.removeItem("cloud_disk_username");
  updateAuthUi();
  updateBreadcrumb();
  $("file-table").innerHTML = '<div class="empty-state">登录后显示你的文件。</div>';
  $("recycle-list").innerHTML = '<div class="empty-state">暂无回收站数据。</div>';
  setStatus("已退出登录。");
}

async function loadProfile() {
  if (!state.token) return;
  const profile = await fetchJson("/api/user/me", {
    headers: authHeaders(),
  });
  $("account-card").innerHTML = `
    <strong>${escapeHtml(profile.username)}</strong>
    <span>已用空间：${formatBytes(profile.storage_used)}</span>
  `;
}

async function createFolder() {
  const name = $("folder-name").value.trim();
  if (!name) throw new Error("请输入文件夹名称。");
  await fetchJson("/api/folders", {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({ parent_id: String(state.currentFolderId), name }),
  });
  $("folder-name").value = "";
  setStatus("文件夹创建成功。");
  await loadFiles();
}

async function loadFiles() {
  if (!state.token) return;
  updateBreadcrumb();
  const files = await fetchJson(`/api/files?parent_id=${state.currentFolderId}`, {
    headers: authHeaders(),
  });
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
      const type = file.is_dir === "true" ? "夹" : "文";
      const openButton = file.is_dir === "true"
        ? `<button class="secondary" data-open-folder="${file.id}" data-folder-name="${escapeHtml(file.name)}">打开</button>`
        : `<button class="secondary" data-download="${file.id}">下载</button>`;
      const shareButton = file.is_dir === "true" ? "" : `<button class="secondary" data-share="${file.id}">分享</button>`;
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
  const response = await fetch(`/api/files/upload?parent_id=${state.currentFolderId}&name=${encodeURIComponent(file.name)}`, {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/octet-stream" }),
    body: file,
  });
  const doc = await response.json();
  if (!response.ok || doc.code !== 0) throw new Error(doc.message || "上传失败。");
  $("instant-hash").value = doc.data.sha256 || "";
  $("instant-size").value = doc.data.size_bytes || "";
  $("instant-name").value = `copy-${file.name}`;
  setUploadProgress(100);
  setStatus(`上传完成：${file.name}。哈希已填入秒传区域。`);
  setTimeout(hideUploadProgress, 500);
}

async function uploadChunked(file) {
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
    setUploadProgress(100);
    setStatus(`服务端已有相同内容，已秒传：${file.name}。`);
    setTimeout(hideUploadProgress, 500);
    return;
  }

  for (let index = 0; index < totalChunks; index += 1) {
    const start = index * chunkSize;
    const end = Math.min(file.size, start + chunkSize);
    await fetch(`/api/uploads/chunk?upload_id=${session.upload_id}&chunk_index=${index}`, {
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
  $("instant-hash").value = completed.sha256 || hash;
  $("instant-size").value = completed.size_bytes || String(file.size);
  $("instant-name").value = `copy-${file.name}`;
  setUploadProgress(100);
  setStatus(`分片上传完成：${file.name}。`);
  setTimeout(hideUploadProgress, 500);
}

async function instantUpload() {
  const name = $("instant-name").value.trim();
  const sha256 = $("instant-hash").value.trim();
  const sizeBytes = $("instant-size").value.trim();
  if (!name || !sha256 || !sizeBytes) throw new Error("请填写秒传文件名、哈希和大小。");
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
  setStatus("秒传文件创建成功。");
  await Promise.all([loadFiles(), loadProfile()]);
}

async function downloadFile(id) {
  const response = await fetch(`/api/files/download?id=${id}`, {
    headers: authHeaders(),
  });
  if (!response.ok) throw new Error("下载失败。");
  const blob = await response.blob();
  const disposition = response.headers.get("Content-Disposition") || "";
  const match = disposition.match(/filename="([^"]+)"/);
  const filename = match ? match[1] : `file-${id}`;
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  link.click();
  URL.revokeObjectURL(url);
  setStatus(`已开始下载：${filename}`);
}

async function deleteFile(id) {
  await fetchJson(`/api/files?id=${id}`, {
    method: "DELETE",
    headers: authHeaders(),
  });
  setStatus("文件已移入回收站。");
  await Promise.all([loadFiles(), loadRecycle(), loadProfile()]);
}

async function loadRecycle() {
  if (!state.token) return;
  const files = await fetchJson("/api/recycle", {
    headers: authHeaders(),
  });
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
  await fetchJson(`/api/recycle/restore?id=${id}`, {
    method: "POST",
    headers: authHeaders(),
  });
  setStatus("文件已恢复。");
  await Promise.all([loadFiles(), loadRecycle()]);
}

async function permanentDelete(id) {
  await fetchJson(`/api/recycle/permanent?id=${id}`, {
    method: "DELETE",
    headers: authHeaders(),
  });
  setStatus("文件已永久删除。");
  await Promise.all([loadRecycle(), loadProfile()]);
}

async function shareFile(id) {
  const accessCode = $("share-code").value.trim();
  const data = await fetchJson("/api/shares", {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({
      file_id: String(id),
      access_code: accessCode,
      allow_download: "true",
    }),
  });
  const url = `${location.origin}${data.url}${accessCode ? `&code=${encodeURIComponent(accessCode)}` : ""}`;
  $("share-output").value = url;
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
  setStatus(error.message || String(error), true);
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
  updateAuthUi();
  updateBreadcrumb();
  if (state.showcaseMode) {
    setStatus("当前是 GitHub Pages 展示模式。完整上传下载功能请在本机启动后端后访问 http://127.0.0.1:8080。");
    return;
  }
  if (state.token) {
    try {
      await Promise.all([loadProfile(), loadFiles(), loadRecycle()]);
      setStatus("已恢复上次登录状态。");
    } catch (error) {
      logout();
      showError(error);
    }
  }
}

init();

