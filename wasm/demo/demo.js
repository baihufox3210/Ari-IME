import {createAriIme} from "../src/index.mjs";

const input = document.querySelector("#key-capture");
const preedit = document.querySelector("#preedit");
const committed = document.querySelector("#committed");
const status = document.querySelector("#engine-status");
const hint = document.querySelector("#hint");
const panel = document.querySelector("#candidate-panel");
const candidates = document.querySelector("#candidates");
const pageLabel = document.querySelector("#page-label");
const learningToggle = document.querySelector("#learning-toggle");
const fullwidthToggle = document.querySelector("#fullwidth-toggle");
const layoutSelect = document.querySelector("#layout-select");

let ime;
let output = "";

function render(state) {
  preedit.textContent = state.preedit || "　";
  if (state.notification) hint.textContent = state.notification;
  else if (state.preedit) hint.textContent = "Enter 送出 · ↓ 開啟候選 · Esc 清除";
  else hint.textContent = "按 Enter 送出；按 ↓ 開啟候選。";

  panel.hidden = !state.picking;
  candidates.replaceChildren();
  if (state.picking) {
    pageLabel.textContent = state.candidatePageCount > 1
      ? `（${state.candidatePage + 1}/${state.candidatePageCount}）`
      : "";
    state.candidates.forEach((candidate, index) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "candidate-option";
      button.setAttribute("aria-pressed", String(index === state.highlight));
      button.innerHTML = `<span class="number">${index + 1}</span>`;
      button.append(document.createTextNode(candidate));
      button.addEventListener("pointerdown", (event) => event.preventDefault());
      button.addEventListener("click", () => render(ime.selectCandidate(index)));
      candidates.append(button);
    });
  }
}

function applyState(state) {
  if (state.hasCommit && state.commitText) {
    output += state.commitText;
    committed.textContent = output || "　";
  }
  render(state);
}

try {
  ime = await createAriIme({learningAllowed: false});
  input.disabled = false;
  status.textContent = "已載入 · 離線執行";
  status.classList.add("ready");
  render(ime.state());
} catch (error) {
  status.textContent = `載入失敗：${error.message}`;
  status.classList.add("error");
  hint.textContent = "請從本機 HTTP 伺服器開啟此頁，並確認 wasm/dist 檔案存在。";
}

input.addEventListener("keydown", (event) => {
  if (!ime || event.isComposing) return;
  if (["Shift", "Control", "Alt", "Meta", "CapsLock"].includes(event.key)) return;
  const isAriShortcut = event.ctrlKey && !event.altKey && !event.metaKey && event.key === " ";
  if ((event.ctrlKey || event.metaKey) && !isAriShortcut) return;

  const state = ime.handleKey(event.key, {
    shift: event.shiftKey,
    ctrl: event.ctrlKey,
    alt: event.altKey,
    meta: event.metaKey,
  });
  if (state.handled) {
    event.preventDefault();
    applyState(state);
  }
});

input.addEventListener("paste", (event) => {
  if (!ime) return;
  const text = event.clipboardData?.getData("text/plain");
  if (text === undefined) return;
  event.preventDefault();
  applyState(ime.paste(text));
});

document.querySelectorAll("[data-example]").forEach((button) => {
  button.addEventListener("click", () => {
    if (!ime) return;
    input.focus();
    let state = ime.reset();
    for (const key of button.dataset.example) state = ime.handleKey(key);
    applyState(state);
  });
});

document.querySelector("#clear-button").addEventListener("click", () => {
  if (!ime) return;
  output = "";
  committed.textContent = "　";
  input.value = "";
  input.focus();
  applyState(ime.reset());
});

document.querySelector("#page-up").addEventListener("click", () => {
  if (ime) applyState(ime.handleKey("PageUp"));
});
document.querySelector("#page-down").addEventListener("click", () => {
  if (ime) applyState(ime.handleKey("PageDown"));
});
learningToggle.addEventListener("change", () => ime?.setLearningAllowed(learningToggle.checked));
fullwidthToggle.addEventListener("change", () => ime?.setFullWidthPunctuation(fullwidthToggle.checked));
layoutSelect.addEventListener("change", () => {
  if (!ime) return;
  ime.setKeyboardLayout(layoutSelect.value);
  applyState(ime.reset());
  input.focus();
});

window.addEventListener("pagehide", () => ime?.dispose(), {once: true});
