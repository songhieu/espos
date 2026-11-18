import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// The Go server embeds the build (../internal/server/dist) and serves index.html with the
// app's config filled in. `npm run dev` proxies the API to a server on :8080.
const env = (globalThis as { process?: { env: Record<string, string | undefined> } }).process?.env ?? {};
const api = env.ESPOS_DEV_API ?? "http://localhost:8080";

export default defineConfig({
  plugins: [react()],
  base: "/",
  build: {
    outDir: "../internal/server/dist",
    emptyOutDir: true,
    rollupOptions: {
      output: {
        // React in a chunk of its own: it changes far less often than the app.
        manualChunks: { react: ["react", "react-dom", "react-dom/client"] },
      },
    },
  },
  server: {
    proxy: Object.fromEntries(["/v1", "/auth", "/files", "/firmware"].map((p) => [p, { target: api, changeOrigin: false }])),
  },
});
