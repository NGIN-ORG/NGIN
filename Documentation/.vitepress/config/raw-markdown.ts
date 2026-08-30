import { existsSync, readFileSync, readdirSync } from "node:fs";
import { extname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";

const documentationRoot = fileURLToPath(new URL("../..", import.meta.url));
const excludedDirectories = new Set([".vitepress", "node_modules"]);

function markdownFiles(directory = documentationRoot): string[] {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (entry.name.startsWith(".") || excludedDirectories.has(entry.name)) {
      return [];
    }

    const absolutePath = resolve(directory, entry.name);
    if (entry.isDirectory()) {
      return markdownFiles(absolutePath);
    }

    return extname(entry.name) === ".md" ? [absolutePath] : [];
  });
}

function portablePath(path: string): string {
  return path.split(sep).join("/");
}

function withoutFrontmatter(source: string): string {
  return source.replace(/^---\r?\n[\s\S]*?\r?\n---\r?\n/, "");
}

function pageTitle(source: string, fallback: string): string {
  const frontmatterTitle = source.match(/^---[\s\S]*?^title:\s*["']?(.+?)["']?\s*$[\s\S]*?^---/m)?.[1];
  const headingTitle = source.match(/^#\s+(.+)$/m)?.[1];
  return (frontmatterTitle ?? headingTitle ?? fallback).replace(/["']/g, "");
}

function pageDescription(source: string): string | undefined {
  return source.match(/^---[\s\S]*?^description:\s*["']?(.+?)["']?\s*$[\s\S]*?^---/m)?.[1]?.replace(/["']/g, "");
}

function sitePath(base: string, path: string): string {
  return `${base}${path.replace(/^\//, "")}`;
}

function llmsIndex(files: string[], base: string): string {
  const groups = new Map<string, string[]>();
  for (const file of files) {
    const path = portablePath(relative(documentationRoot, file));
    const section = path.includes("/") ? path.split("/")[0] : "Overview";
    const source = readFileSync(file, "utf8");
    const title = pageTitle(source, path);
    const description = pageDescription(source);
    const item = `- [${title}](${sitePath(base, `raw/${path}`)})${description ? `: ${description}` : ""}`;
    groups.set(section, [...(groups.get(section) ?? []), item]);
  }

  const sections = [...groups.entries()]
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([section, items]) => `## ${section}\n\n${items.join("\n")}`)
    .join("\n\n");

  return `# NGIN Documentation\n\n> NGIN is an experimental project system and modular C++23 application toolkit. These Markdown pages are the canonical documentation source for this site. Prefer a focused page or library bundle over the full corpus.\n\n${sections}\n`;
}

function fullBundle(files: string[], heading: string): string {
  const pages = files.map((file) => {
    const path = portablePath(relative(documentationRoot, file));
    const source = withoutFrontmatter(readFileSync(file, "utf8")).trim();
    return `<!-- source: /raw/${path} -->\n\n${source}`;
  });
  return `# ${heading}\n\n${pages.join("\n\n---\n\n")}\n`;
}

function filesForComponent(files: string[], component: string): string[] {
  return files.filter((file) => {
    const path = portablePath(relative(documentationRoot, file));
    return path === `libraries/${component}.md` ||
      path.startsWith(`libraries/${component}/`) ||
      path === `api/${component}.md` ||
      path.startsWith(`api/${component}/`) ||
      path === `reference/cpp/${component}.md` ||
      path.startsWith(`reference/cpp/${component}/`);
  });
}

function filesForApi(files: string[]): string[] {
  return files.filter((file) => {
    const path = portablePath(relative(documentationRoot, file));
    return path === "api.md" || path.startsWith("api/") ||
      path === "reference/cpp.md" || path.startsWith("reference/cpp/");
  });
}

function componentTitle(component: string): string {
  if (component === "ecs") {
    return "NGIN.ECS documentation";
  }
  if (component === "ui") {
    return "NGIN.UI documentation";
  }
  return `NGIN.${component[0].toUpperCase() + component.slice(1)} documentation`;
}

function pageRoute(path: string): string {
  if (path === "index.md") {
    return "";
  }
  return path.replace(/\.md$/, "");
}

function redirectPage(destination: string): string {
  const encodedDestination = JSON.stringify(destination);
  return `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="robots" content="noindex">
    <meta http-equiv="refresh" content="0; url=${destination}">
    <link rel="canonical" href="${destination}">
    <title>Redirecting to NGIN Documentation</title>
    <script>location.replace(${encodedDestination} + location.search + location.hash)</script>
  </head>
  <body><p><a href="${destination}">Continue to NGIN Documentation</a></p></body>
</html>`;
}

type RawMarkdownOptions = {
  base: string;
};

function stripBase(pathname: string, base: string): string {
  if (base === "/" || !pathname.startsWith(base)) {
    return pathname;
  }
  return `/${pathname.slice(base.length)}`;
}

function redirectTemporaryMount(request: { url?: string }, response: {
  statusCode: number;
  setHeader(name: string, value: string): void;
  end(): void;
}, base: string): boolean {
  if (base !== "/") {
    return false;
  }

  const pathname = decodeURIComponent((request.url ?? "").split("?")[0]);
  const mount = ["/Documentation", "/NGIN"].find((candidate) =>
    pathname === candidate || pathname.startsWith(`${candidate}/`));
  if (!mount) {
    return false;
  }

  const destination = pathname.slice(mount.length) || "/";
  response.statusCode = 302;
  response.setHeader("Location", destination);
  response.end();
  return true;
}

function redirectLegacyLibraryPath(request: { url?: string }, response: {
  statusCode: number;
  setHeader(name: string, value: string): void;
  end(): void;
}, base: string): boolean {
  const requestedUrl = decodeURIComponent((request.url ?? "").split("?")[0]);
  const pathname = stripBase(requestedUrl, base);
  if (pathname !== "/components" && !pathname.startsWith("/components/")) {
    return false;
  }

  const destination = sitePath(base, pathname.replace(/^\/components/, "libraries"));
  response.statusCode = 302;
  response.setHeader("Location", destination);
  response.end();
  return true;
}

export function rawMarkdownPlugin(options: RawMarkdownOptions): Plugin {
  return {
    name: "ngin-raw-markdown",
    enforce: "pre",
    async resolveId(source, importer) {
      if (!/\.md\?t=\d+(?:&|$)/.test(source)) {
        return null;
      }

      const withoutTimestamp = source
        .replace(/\?t=\d+&?/, "?")
        .replace(/[?&]$/, "");
      return await this.resolve(withoutTimestamp, importer, { skipSelf: true });
    },
    configureServer(server) {
      server.middlewares.use((request, response, next) => {
        if (redirectTemporaryMount(request, response, options.base)) {
          return;
        }
        if (redirectLegacyLibraryPath(request, response, options.base)) {
          return;
        }

        const requestedUrl = decodeURIComponent((request.url ?? "").split("?")[0]);
        const pathname = stripBase(requestedUrl, options.base);
        const files = markdownFiles().sort();

        if (pathname === "/llms.txt") {
          response.statusCode = 200;
          response.setHeader("Content-Type", "text/plain; charset=utf-8");
          response.end(llmsIndex(files, options.base));
          return;
        }

        if (pathname === "/llms-full.txt") {
          response.statusCode = 200;
          response.setHeader("Content-Type", "text/plain; charset=utf-8");
          response.end(fullBundle(files, "NGIN complete documentation"));
          return;
        }

        const componentMatch = pathname.match(/^\/llms\/(base|core|reflection|ecs|ui|log)\.txt$/);
        if (componentMatch) {
          const component = componentMatch[1];
          response.statusCode = 200;
          response.setHeader("Content-Type", "text/plain; charset=utf-8");
          response.end(fullBundle(filesForComponent(files, component), componentTitle(component)));
          return;
        }

        if (pathname === "/llms/api.txt") {
          response.statusCode = 200;
          response.setHeader("Content-Type", "text/plain; charset=utf-8");
          response.end(fullBundle(filesForApi(files), "NGIN C++ API reference and guides"));
          return;
        }

        if (!pathname.startsWith("/raw/")) {
          next();
          return;
        }

        const requestedPath = pathname.slice(5);
        if (!requestedPath.endsWith(".md")) {
          next();
          return;
        }

        const absolutePath = resolve(documentationRoot, requestedPath);
        const relativePath = relative(documentationRoot, absolutePath);
        if (relativePath.startsWith("..") || !existsSync(absolutePath)) {
          next();
          return;
        }

        response.statusCode = 200;
        response.setHeader("Content-Type", "text/markdown; charset=utf-8");
        response.end(readFileSync(absolutePath, "utf8"));
      });
    },
    configurePreviewServer(server) {
      server.middlewares.use((request, response, next) => {
        if (redirectTemporaryMount(request, response, options.base)) {
          return;
        }
        if (redirectLegacyLibraryPath(request, response, options.base)) {
          return;
        }
        next();
      });
    },
    generateBundle() {
      const files = markdownFiles().sort();
      for (const file of files) {
        const path = portablePath(relative(documentationRoot, file));
        const source = readFileSync(file, "utf8");
        this.emitFile({ type: "asset", fileName: path, source });
        this.emitFile({ type: "asset", fileName: `raw/${path}`, source });

        if (options.base === "/") {
          const route = pageRoute(path);
          const destination = route ? `/${route}` : "/";
          for (const mount of ["Documentation", "NGIN"]) {
            const alias = route ? `${mount}/${route}.html` : `${mount}/index.html`;
            this.emitFile({ type: "asset", fileName: alias, source: redirectPage(destination) });
          }
        }

        if (path === "libraries.md" || path.startsWith("libraries/")) {
          const legacyRoute = pageRoute(path).replace(/^libraries/, "components");
          this.emitFile({
            type: "asset",
            fileName: `${legacyRoute}.html`,
            source: redirectPage(`/${pageRoute(path)}`)
          });
        }
      }

      if (options.base === "/") {
        this.emitFile({ type: "asset", fileName: "Documentation.html", source: redirectPage("/") });
        this.emitFile({ type: "asset", fileName: "NGIN.html", source: redirectPage("/") });
      }

      this.emitFile({ type: "asset", fileName: "llms.txt", source: llmsIndex(files, options.base) });
      this.emitFile({ type: "asset", fileName: "llms-full.txt", source: fullBundle(files, "NGIN complete documentation") });

      const components = ["base", "core", "reflection", "ecs", "ui", "log"];
      for (const component of components) {
        this.emitFile({
          type: "asset",
          fileName: `llms/${component}.txt`,
          source: fullBundle(filesForComponent(files, component), componentTitle(component))
        });
      }
      this.emitFile({
        type: "asset",
        fileName: "llms/api.txt",
        source: fullBundle(filesForApi(files), "NGIN C++ API reference and guides")
      });
    }
  };
}
