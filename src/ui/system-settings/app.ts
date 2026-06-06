type Language = "ru" | "en";

interface ApiBaseResponse {
  ok: boolean;
}

interface HealthResponse extends ApiBaseResponse {
  service: string;
}

interface StatusResponse extends ApiBaseResponse {
  apiSocket: string;
  apiSocketAvailable: boolean;
  daemon: string;
  dataRoot: string;
  dataRootAvailable: boolean;
  kernel: string;
  language: string;
  manifestDir: string;
  manifestDirAvailable: boolean;
  rootfs: string;
  status: string;
  systemRoot: string;
  systemRootReadOnly: boolean;
  uptimeSeconds: number;
}

interface RolesResponse extends ApiBaseResponse {
  currentRole: string;
  defaultRole: string;
  hasRootAccess: boolean;
  permissions: string[];
  rootHashConfigured: boolean;
  rootRole: string;
  rootSessionUnlocked: boolean;
  userCreation: string;
}

interface AppManifest {
  capability: string;
  description: string;
  name: string;
  runtime: string;
  uiEntry: string;
  version: string;
}

interface AppsResponse extends ApiBaseResponse {
  apps: AppManifest[];
}

interface RunResponse extends ApiBaseResponse {
  exitCode: number;
  output: string;
}

interface ErrorResponse {
  error?: string;
  ok?: boolean;
  output?: string;
}

const state: { apps: AppManifest[] } = {
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
    notifications: "Уведомления",
    network: "Сеть",
    local: "локально",
    power: "Питание",
    vm: "VM",
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
    notifications: "Notifications",
    network: "Network",
    local: "local",
    power: "Power",
    vm: "VM",
  },
} as const;

type LabelKey = keyof (typeof labels)["ru"];

const el = {
  apps: requiredElement<HTMLDivElement>("#apps"),
  appsCount: requiredElement<HTMLSpanElement>("#apps-count"),
  clearOutput: requiredElement<HTMLButtonElement>("#clear-output"),
  clock: requiredElement<HTMLSpanElement>("#clock"),
  health: requiredElement<HTMLSpanElement>("#health"),
  network: requiredElement<HTMLSpanElement>("#network"),
  notifications: requiredElement<HTMLSpanElement>("#notifications"),
  output: requiredElement<HTMLPreElement>("#command-output"),
  power: requiredElement<HTMLSpanElement>("#power"),
  refresh: requiredElement<HTMLButtonElement>("#refresh"),
  roleName: requiredElement<HTMLSpanElement>("#role-name"),
  roles: requiredElement<HTMLPreElement>("#roles"),
  status: requiredElement<HTMLPreElement>("#status"),
  summary: requiredElement<HTMLParagraphElement>("#summary"),
};

function requiredElement<T extends HTMLElement>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (element === null) {
    throw new Error(`Missing UI element: ${selector}`);
  }
  return element;
}

async function api<T extends ApiBaseResponse>(path: string): Promise<T> {
  const response = await fetch(path, { cache: "no-store" });
  const data = (await response.json()) as T & ErrorResponse;
  if (!response.ok || data.ok === false) {
    const message = data.output ?? data.error ?? `HTTP ${response.status}`;
    throw new Error(message.trim());
  }
  return data;
}

function installCloseGuard(): void {
  window.addEventListener("beforeunload", (event) => {
    event.preventDefault();
    event.returnValue = "";
  });
}

function setOutput(text: string, isError = false): void {
  el.output.textContent = text;
  el.output.classList.toggle("error", isError);
}

function uiLanguage(value: string): Language {
  return value === "en" ? "en" : "ru";
}

function label(language: Language, key: LabelKey): string {
  return labels[language][key];
}

function updateClock(
  language: Language = uiLanguage(document.documentElement.lang),
): void {
  const locale = language === "en" ? "en-US" : "ru-RU";
  el.clock.textContent = new Intl.DateTimeFormat(locale, {
    day: "2-digit",
    hour: "2-digit",
    hour12: false,
    minute: "2-digit",
    month: "2-digit",
  }).format(new Date());
}

function renderSystemStrip(language: Language): void {
  updateClock(language);
  el.notifications.textContent = `${label(language, "notifications")}: 0`;
  el.network.textContent = `${label(language, "network")}: ${label(language, "local")}`;
  el.power.textContent = `${label(language, "power")}: ${label(language, "vm")}`;
}

function formatStatus(status: StatusResponse): string {
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

function formatRoles(roles: RolesResponse, language: Language): string {
  return [
    `${label(language, "currentRole")}: ${roles.currentRole}`,
    `${label(language, "defaultRole")}: ${roles.defaultRole}`,
    `${label(language, "rootRole")}: ${roles.rootRole}`,
    `${label(language, "permissions")}: ${roles.permissions.join(", ")}`,
    `${label(language, "rootAccess")}: ${roles.hasRootAccess ? label(language, "allow") : label(language, "deny")}`,
    `${label(language, "rootHash")}: ${roles.rootHashConfigured ? label(language, "configured") : label(language, "missing")}`,
    `${label(language, "rootSession")}: ${roles.rootSessionUnlocked ? label(language, "unlocked") : label(language, "locked")}`,
    `${label(language, "userCreation")}: ${roles.userCreation}`,
  ].join("\n");
}

function renderApps(): void {
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
    button.addEventListener("click", () => {
      void runApp(app.name);
    });

    row.append(meta, button);
    el.apps.append(row);
  }
}

async function refresh(): Promise<void> {
  el.health.textContent = "...";
  try {
    const [health, status, roles, apps] = await Promise.all([
      api<HealthResponse>("/health"),
      api<StatusResponse>("/api/status"),
      api<RolesResponse>("/api/roles"),
      api<AppsResponse>("/api/apps"),
    ]);

    const language = uiLanguage(status.language);
    document.documentElement.lang = language;
    renderSystemStrip(language);
    el.health.textContent = health.ok ? "online" : "error";
    el.summary.textContent = `${health.service} · 127.0.0.1:8080`;
    el.status.textContent = formatStatus(status);
    el.roles.textContent = formatRoles(roles, language);
    el.roleName.textContent = roles.currentRole || "...";
    state.apps = apps.apps.filter((app) => app.uiEntry !== "internal");
    renderApps();
  } catch (error) {
    el.health.textContent = "error";
    setOutput(error instanceof Error ? error.message : String(error), true);
  }
}

async function runApp(name: string): Promise<void> {
  setOutput(`$ ${name}\n...`);
  try {
    const result = await api<RunResponse>(
      `/api/run?name=${encodeURIComponent(name)}`,
    );
    setOutput(result.output);
  } catch (error) {
    setOutput(error instanceof Error ? error.message : String(error), true);
  }
}

el.refresh.addEventListener("click", () => {
  void refresh();
});
el.clearOutput.addEventListener("click", () => {
  setOutput("");
});

installCloseGuard();
void refresh();
window.setInterval(() => {
  updateClock();
}, 30_000);
