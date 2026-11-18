import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "./App";
import { readConfig } from "./config";
import "./app.css";

// The app's type: Be Vietnam Pro for text, Newsreader for titles (both carry Vietnamese); the device's
// own fonts (Noto Sans, Noto Serif, Ubuntu) for the screen previews.
if (!document.getElementById("ik-fonts")) {
  const link = document.createElement("link");
  link.id = "ik-fonts";
  link.rel = "stylesheet";
  link.href = "https://fonts.googleapis.com/css2?family=Be+Vietnam+Pro:wght@400;500;600;700&family=Newsreader:opsz,wght@6..72,500;6..72,600&family=Noto+Sans:ital,wght@0,400;0,700;1,400&family=Noto+Serif:wght@700&family=Ubuntu:wght@400;700&display=swap";
  document.head.appendChild(link);
}

// ONE module for every mount: a page may mount this app more than once
// ({% app 'espos', root: 'second' %}), and each root has its own config.
for (const el of document.querySelectorAll<HTMLElement>('[data-wm-app="espos"]')) {
  createRoot(el).render(
    <StrictMode>
      <App config={readConfig(el)} />
    </StrictMode>,
  );
}
