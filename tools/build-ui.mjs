import { execFileSync } from "node:child_process";
import { cpSync, mkdirSync, rmSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const sourceDir = resolve(rootDir, "src/ui/system-settings");
const outDir = resolve(rootDir, "build/ui");

rmSync(outDir, { force: true, recursive: true });
mkdirSync(outDir, { recursive: true });

cpSync(resolve(sourceDir, "index.html"), resolve(outDir, "index.html"));
cpSync(resolve(sourceDir, "styles.css"), resolve(outDir, "styles.css"));

execFileSync(
  resolve(rootDir, "node_modules/.bin/tsc"),
  ["-p", "tsconfig.ui.json"],
  {
    cwd: rootDir,
    stdio: "inherit",
  },
);
