import { spawnSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { symbolLinks } from "./reference-links.mjs";
import { libraries } from "../doxygen/libraries.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const output = resolve(root, ".vitepress/doxygen");
const published = resolve(root, "public/reference/doxygen");
const mainpage = readFileSync(resolve(root, "doxygen/mainpage.dox"), "utf8");
for (const asset of ["doxygen-awesome.css", "LICENSE"]) {
  if (!existsSync(resolve(root, "node_modules/@jothepro/doxygen-awesome-css", asset))) {
    throw new Error(`Missing Doxygen Awesome asset: ${asset}. Run npm ci in Documentation before generating the reference.`);
  }
}
for (const { input } of libraries) {
  if (!existsSync(resolve(root, input))) {
    throw new Error(`Missing public headers: ${input}. Initialize the repository submodules before building documentation.`);
  }
}

const version = spawnSync("doxygen", ["--version"], { encoding: "utf8" });
if (version.error || version.status !== 0) {
  throw new Error("Doxygen is required. Install Doxygen 1.9.8 or newer and ensure doxygen is on PATH.");
}
const [major, minor, patch] = version.stdout.trim().split(".").map(Number);
if (!(major > 1 || (major === 1 && (minor > 9 || (minor === 9 && patch >= 8))))) {
  throw new Error(`Doxygen 1.9.8 or newer is required; found ${version.stdout.trim()}.`);
}

rmSync(output, { recursive: true, force: true });
mkdirSync(output, { recursive: true });

function generate(library, linkDependencies) {
  const { id, name, input, guide } = library;
  const directory = `.vitepress/doxygen/${id}`;
  mkdirSync(resolve(root, directory), { recursive: true });
  writeFileSync(resolve(root, directory, "mainpage.dox"), mainpage
    .replaceAll("@LIBRARY@", name).replaceAll("@GUIDE@", guide));
  const tagFiles = linkDependencies ? libraries.filter((other) => library.dependencies.includes(other.id)).map((other) =>
    `.vitepress/doxygen/${other.id}/${other.id}.tag=../${other.id}`) : [];
  const config = [
    "@INCLUDE = doxygen/Doxyfile",
    `PROJECT_NAME = "${name} C++ API"`,
    `OUTPUT_DIRECTORY = ${directory}`,
    `INPUT = ${input} ${directory}/mainpage.dox`,
    `GENERATE_TAGFILE = ${directory}/${id}.tag`,
    "GENERATE_HTML = YES",
    `WARN_LOGFILE = ${directory}/${linkDependencies ? "warnings" : "local-warnings"}.log`,
    `TAGFILES = ${tagFiles.join(" ")}`
  ].join("\n");
  const result = spawnSync("doxygen", ["-"], { cwd: root, input: config, encoding: "utf8", stdio: ["pipe", "inherit", "inherit"] });
  if (result.error || result.status !== 0) throw new Error(`Doxygen generation failed for ${name}; inspect ${directory}.`);
}

// Doxygen adds imported tags to search. Keep its independently generated search
// index, then generate declaration pages with links to dependency tag files.
for (const library of libraries) {
  generate(library, false);
  cpSync(resolve(output, library.id, "html/search"), resolve(output, library.id, "local-search"), { recursive: true });
}
for (const library of libraries) {
  generate(library, true);
  const directory = resolve(output, library.id);
  rmSync(resolve(directory, "html/search"), { recursive: true, force: true });
  cpSync(resolve(directory, "local-search"), resolve(directory, "html/search"), { recursive: true });
  const symbols = symbolLinks(readFileSync(resolve(directory, `${library.id}.tag`), "utf8"));
  if (!symbols[library.symbol]) throw new Error(`Doxygen did not generate expected public symbol ${library.symbol}.`);
  writeFileSync(resolve(directory, "symbols.json"), JSON.stringify(symbols));
  console.log(`Generated ${library.name} reference (${Object.keys(symbols).length} symbol links).`);
  if (readFileSync(resolve(directory, "warnings.log"), "utf8").trim()) {
    console.warn(`Doxygen reported warnings for ${library.name}; see .vitepress/doxygen/${library.id}/warnings.log.`);
  }
}

// Replace the published tree only after all libraries have generated successfully.
rmSync(published, { recursive: true, force: true });
for (const { id } of libraries) {
  cpSync(resolve(output, id, "html"), resolve(published, id), { recursive: true });
}
