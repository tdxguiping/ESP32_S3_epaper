(function () {
  const BASIC_FG = {
    30: "#000000", 31: "#cd3131", 32: "#0dbc79", 33: "#e5e510",
    34: "#2472c8", 35: "#bc3fbc", 36: "#11a8cd", 37: "#e5e5e5",
    90: "#666666", 91: "#f14c4c", 92: "#23d18b", 93: "#f5f543",
    94: "#3b8eea", 95: "#d670d6", 96: "#29b8db", 97: "#ffffff",
  };
  const BASIC_BG = {
    40: "#000000", 41: "#cd3131", 42: "#0dbc79", 43: "#e5e510",
    44: "#2472c8", 45: "#bc3fbc", 46: "#11a8cd", 47: "#e5e5e5",
    100: "#666666", 101: "#f14c4c", 102: "#23d18b", 103: "#f5f543",
    104: "#3b8eea", 105: "#d670d6", 106: "#29b8db", 107: "#ffffff",
  };

  function escapeHtml(text) {
    return String(text || "").replace(/[&<>"']/g, (ch) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      "\"": "&quot;",
      "'": "&#39;",
    }[ch]));
  }

  function xterm256Color(index) {
    const n = Math.max(0, Math.min(255, Number(index) || 0));
    const system = [
      "#000000", "#800000", "#008000", "#808000", "#000080", "#800080", "#008080", "#c0c0c0",
      "#808080", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff",
    ];
    if (n < 16) return system[n];
    if (n >= 232) {
      const level = 8 + (n - 232) * 10;
      const hex = level.toString(16).padStart(2, "0");
      return `#${hex}${hex}${hex}`;
    }
    const value = [0, 95, 135, 175, 215, 255];
    const offset = n - 16;
    const r = value[Math.floor(offset / 36) % 6];
    const g = value[Math.floor(offset / 6) % 6];
    const b = value[offset % 6];
    return `#${r.toString(16).padStart(2, "0")}${g.toString(16).padStart(2, "0")}${b.toString(16).padStart(2, "0")}`;
  }

  function styleText(state) {
    const parts = [];
    if (state.bold) parts.push("font-weight:700");
    if (state.fg) parts.push(`color:${state.fg}`);
    if (state.bg) parts.push(`background-color:${state.bg}`);
    return parts.join(";");
  }

  function applySgr(paramsText, state) {
    const params = paramsText === "" ? [0] : paramsText.split(";").map((part) => part === "" ? 0 : Number(part));
    for (let i = 0; i < params.length; i++) {
      const code = Number.isFinite(params[i]) ? params[i] : 0;
      if (code === 0) {
        state.bold = false;
        state.fg = "";
        state.bg = "";
      } else if (code === 1) {
        state.bold = true;
      } else if (code === 22) {
        state.bold = false;
      } else if (code === 39) {
        state.fg = "";
      } else if (code === 49) {
        state.bg = "";
      } else if (BASIC_FG[code]) {
        state.fg = BASIC_FG[code];
      } else if (BASIC_BG[code]) {
        state.bg = BASIC_BG[code];
      } else if ((code === 38 || code === 48) && params[i + 1] === 5 && i + 2 < params.length) {
        const color = xterm256Color(params[i + 2]);
        if (code === 38) state.fg = color;
        else state.bg = color;
        i += 2;
      }
    }
  }

  function render(text) {
    const input = String(text || "");
    const state = { bold: false, fg: "", bg: "" };
    const csi = /\x1b\[([0-9;]*)([A-Za-z])/g;
    let html = "";
    let last = 0;
    let openStyle = "";

    function appendChunk(chunk) {
      if (!chunk) return;
      const nextStyle = styleText(state);
      if (nextStyle !== openStyle) {
        if (openStyle) html += "</span>";
        openStyle = nextStyle;
        if (openStyle) html += `<span style="${openStyle}">`;
      }
      html += escapeHtml(chunk);
    }

    for (let match = csi.exec(input); match; match = csi.exec(input)) {
      appendChunk(input.slice(last, match.index));
      if (match[2] === "m") {
        applySgr(match[1], state);
      }
      last = match.index + match[0].length;
    }
    appendChunk(input.slice(last));
    if (openStyle) html += "</span>";
    return html;
  }

  window.TdxAnsiTerminalRender = { render };
}());
