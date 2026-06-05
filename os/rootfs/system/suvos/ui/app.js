const state = {
  apps: [],
};

const labels = {
  ru: {
    status: "Статус",
    daemon: "Сервис",
    systemRoot: "Системная зона",
    dataRoot: "Writable-зона",
    manifests: "Манифесты",
    apiSocket: "API socket",
    rootfs: "Rootfs",
    uptime: "Uptime",
    kernel: "Ядро",
    currentRole: "Текущая роль",
    defaultRole: "Роль по умолчанию",
    rootRole: "Root-роль",
    permissions: "Права",
    rootAccess: "Root-доступ",
    rootHash: "Root hash",
    rootSession: "Root session",
    userCreation: "Создание пользователя",
    readOnly: "read-only",
    writable: "writable",
    available: "доступен",
    missing: "отсутствует",
    allow: "разрешен",
    deny: "запрещен",
    configured: "настроен",
    unlocked: "разблокирован",
    locked: "заблокирован",
    seconds: "секунд",
  },
  en: {
    status: "Status",
    daemon: "Daemon",
    systemRoot: "System root",
    dataRoot: "Writable data root",
    manifests: "Manifests",
    apiSocket: "API socket",
    rootfs: "Rootfs",
    uptime: "Uptime",
    kernel: "Kernel",
    currentRole: "Current role",
    defaultRole: "Default role",
    rootRole: "Root role",
    permissions: "Permissions",
    rootAccess: "Root access",
    rootHash: "Root hash",
    rootSession: "Root session",
    userCreation: "User creation",
    readOnly: "read-only",
    writable: "writable",
    available: "available",
    missing: "missing",
    allow: "allow",
    deny: "deny",
    configured: "configured",
    unlocked: "unlocked",
    locked: "locked",
    seconds: "seconds",
  },
};

const el = {
  health: document.querySelector("#health"),
  summary: document.querySelector("#summary"),
  status: document.querySelector("#status"),
  roles: document.querySelector("#roles"),
  roleName: document.querySelector("#role-name"),
  appsCount: document.querySelector("#apps-count"),
  apps: document.querySelector("#apps"),
  output: document.querySelector("#command-output"),
  refresh: document.querySelector("#refresh"),
  clearOutput: document.querySelector("#clear-output"),
};

async function api(path) {
  const response = await fetch(path, { cache: "no-store" });
  const data = await response.json();
  if (!response.ok || data.ok === false) {
    const message = data.output || data.error || `HTTP ${response.status}`;
    throw new Error(message.trim());
  }
  return data;
}

function setOutput(text, isError = false) {
  el.output.textContent = text || "";
  el.output.classList.toggle("error", isError);
}

function uiLanguage(value) {
  return value === "en" ? "en" : "ru";
}

function label(language, key) {
  return labels[language][key];
}

function formatStatus(status) {
  const language = uiLanguage(status.language);
  return [
    `${label(language, "status")}: ${status.status}`,
    `${label(language, "daemon")}: ${status.daemon}`,
    `${label(language, "systemRoot")}: ${status.systemRoot} (${status.systemRootReadOnly ? label(language, "readOnly") : label(language, "writable")})`,
    `${label(language, "dataRoot")}: ${status.dataRoot} (${status.dataRootAvailable ? label(language, "available") : label(language, "missing")})`,
    `${label(language, "manifests")}: ${status.manifestDir} (${status.manifestDirAvailable ? label(language, "available") : label(language, "missing")})`,
    `${label(language, "apiSocket")}: ${status.apiSocket} (${status.apiSocketAvailable ? label(language, "available") : label(language, "missing")})`,
    `${label(language, "rootfs")}: ${status.rootfs}`,
    `${label(language, "uptime")}: ${status.uptimeSeconds} ${label(language, "seconds")}`,
    `${label(language, "kernel")}: ${status.kernel}`,
  ].join("\n");
}

function formatRoles(roles, language) {
  return [
    `${label(language, "currentRole")}: ${roles.currentRole}`,
    `${label(language, "defaultRole")}: ${roles.defaultRole}`,
    `${label(language, "rootRole")}: ${roles.rootRole}`,
    `${label(language, "permissions")}: ${(roles.permissions || []).join(", ")}`,
    `${label(language, "rootAccess")}: ${roles.hasRootAccess ? label(language, "allow") : label(language, "deny")}`,
    `${label(language, "rootHash")}: ${roles.rootHashConfigured ? label(language, "configured") : label(language, "missing")}`,
    `${label(language, "rootSession")}: ${roles.rootSessionUnlocked ? label(language, "unlocked") : label(language, "locked")}`,
    `${label(language, "userCreation")}: ${roles.userCreation}`,
  ].join("\n");
}

function renderApps() {
  el.appsCount.textContent = String(state.apps.length);
  el.apps.replaceChildren();

  for (const app of state.apps) {
    const row = document.createElement("div");
    row.className = "app-row";

    const meta = document.createElement("div");
    const name = document.createElement("div");
    name.className = "app-name";
    name.textContent = app.name;
    const desc = document.createElement("div");
    desc.className = "app-desc";
    desc.textContent = app.description;
    meta.append(name, desc);

    const button = document.createElement("button");
    button.type = "button";
    button.className = "primary";
    button.textContent = "Запуск";
    button.addEventListener("click", () => runApp(app.name));

    row.append(meta, button);
    el.apps.append(row);
  }
}

async function refresh() {
  el.health.textContent = "...";
  try {
    const [health, status, roles, apps] = await Promise.all([
      api("/health"),
      api("/api/status"),
      api("/api/roles"),
      api("/api/apps"),
    ]);

    const language = uiLanguage(status.language);
    document.documentElement.lang = language;
    el.health.textContent = health.ok ? "online" : "error";
    el.summary.textContent = "127.0.0.1:8080";
    el.status.textContent = formatStatus(status);
    el.roles.textContent = formatRoles(roles, language);
    el.roleName.textContent = roles.currentRole || "...";
    state.apps = Array.isArray(apps.apps) ? apps.apps : [];
    renderApps();
  } catch (error) {
    el.health.textContent = "error";
    setOutput(error.message, true);
  }
}

async function runApp(name) {
  setOutput(`$ ${name}\n...`);
  try {
    const result = await api(`/api/run?name=${encodeURIComponent(name)}`);
    setOutput(result.output || "");
  } catch (error) {
    setOutput(error.message, true);
  }
}

el.refresh.addEventListener("click", refresh);
el.clearOutput.addEventListener("click", () => setOutput(""));

refresh();
