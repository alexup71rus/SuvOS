const state = {
  apps: [],
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

function parseApps(output) {
  return output
    .split("\n")
    .filter((line) => line.startsWith("  "))
    .map((line) => {
      const value = line.trim();
      const dash = value.indexOf(" - ");
      if (dash === -1) {
        return { name: value, description: "" };
      }
      return {
        name: value.slice(0, dash),
        description: value.slice(dash + 3),
      };
    });
}

function roleFromOutput(output) {
  const line = output.split("\n").find((item) => item.includes(":"));
  if (!line) {
    return "...";
  }
  return line.slice(line.indexOf(":") + 1).trim() || "...";
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

    el.health.textContent = health.ok ? "online" : "error";
    el.summary.textContent = "127.0.0.1:8080";
    el.status.textContent = status.output;
    el.roles.textContent = roles.output;
    el.roleName.textContent = roleFromOutput(roles.output);
    state.apps = parseApps(apps.output);
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
