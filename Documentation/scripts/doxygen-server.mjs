import { readFile, stat } from "node:fs/promises";
import { extname, isAbsolute, relative, resolve, sep } from "node:path";

const contentTypes = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".gif": "image/gif",
  ".woff": "font/woff",
  ".woff2": "font/woff2"
};

export function doxygenMiddleware(root, base) {
  const prefix = `${base}reference/doxygen/`;
  return async (request, response, next) => {
    const pathname = (request.url ?? "").split("?")[0];
    if (!pathname.startsWith(prefix)) return next();
    if (request.method !== "GET" && request.method !== "HEAD") {
      response.writeHead(405, { Allow: "GET, HEAD" });
      response.end();
      return;
    }
    let file;
    try {
      file = resolve(root, decodeURIComponent(pathname.slice(prefix.length)));
    } catch {
      response.writeHead(400);
      response.end("Invalid reference URL.");
      return;
    }
    const path = relative(root, file);
    if (isAbsolute(path) || path === ".." || path.startsWith(`..${sep}`)) {
      response.writeHead(404);
      response.end("Reference not found.");
      return;
    }
    try {
      if ((await stat(file)).isDirectory()) {
        response.writeHead(302, { Location: `${pathname.replace(/\/$/, "")}/index.html` });
        response.end();
        return;
      }
      const body = await readFile(file);
      response.writeHead(200, {
        "Content-Type": contentTypes[extname(file)] ?? "application/octet-stream",
        "Content-Length": body.length,
        "Cache-Control": "no-cache"
      });
      response.end(request.method === "HEAD" ? undefined : body);
    } catch (error) {
      if (error.code !== "ENOENT" && error.code !== "ENOTDIR") return next(error);
      response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
      response.end("Reference not found. Run npm run reference in Documentation to regenerate it.");
    }
  };
}

export function doxygenServerPlugin({ base }) {
  return {
    name: "ngin-doxygen-server",
    enforce: "pre",
    configureServer(server) {
      // Read from disk on each request: generation replaces the output tree,
      // which can leave Vite's cached public-file list out of date.
      server.middlewares.use(doxygenMiddleware(resolve(server.config.publicDir, "reference/doxygen"), base));
    }
  };
}
