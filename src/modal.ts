import { invoke } from "@tauri-apps/api/core";

const scrim = document.getElementById("scrim") as HTMLDivElement;

export interface SshParams {
  host: string;
  port: number;
  username: string;
  password: string;
}

export interface SerialParams {
  port: string;
  baudRate: number;
  dataBits: number;
  parity: number;
  stopBits: number;
}

export type ConnectionKind = "ssh" | "serial";

function closeModal() {
  scrim.classList.remove("open");
  scrim.innerHTML = "";
}

function mount(html: string): HTMLDivElement {
  scrim.innerHTML = html;
  scrim.classList.add("open");
  const modal = scrim.querySelector(".modal") as HTMLDivElement;
  scrim.onclick = (e) => {
    if (e.target === scrim) closeModal();
  };
  return modal;
}

document.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && scrim.classList.contains("open")) {
    e.preventDefault();
    closeModal();
  }
});

// ---------------------------------------------------------------------------
// Type chooser — first stop when user clicks "+"
// ---------------------------------------------------------------------------
export function openTypeChooser(onPick: (kind: ConnectionKind) => void) {
  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <div>
          <div class="modal-title">Nouvelle connexion</div>
          <div class="modal-sub">Choisissez le type de connexion à l'équipement</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="type-grid">
        <button class="type-card ssh" data-kind="ssh">
          <span class="type-card-badge">SSH</span>
          <span class="type-card-title">SSH</span>
          <span class="type-card-sub">Switch, routeur, serveur — port 22 par défaut</span>
        </button>
        <button class="type-card com" data-kind="serial">
          <span class="type-card-badge">COM</span>
          <span class="type-card-title">Série</span>
          <span class="type-card-sub">Console RJ45, USB-COM, DB9</span>
        </button>
      </div>
      <div class="modal-btns">
        <button class="btn btn-secondary" id="m-cancel">Annuler</button>
      </div>
    </div>
  `);

  modal.querySelectorAll<HTMLButtonElement>(".type-card").forEach((card) => {
    card.onclick = () => {
      const kind = card.dataset.kind as ConnectionKind;
      closeModal();
      onPick(kind);
    };
  });
  (modal.querySelector("#m-cancel") as HTMLButtonElement).onclick = closeModal;
}

// ---------------------------------------------------------------------------
// SSH modal
// ---------------------------------------------------------------------------
export function openSshModal(onConnect: (p: SshParams) => Promise<void>) {
  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <span class="modal-badge">SSH</span>
        <div>
          <div class="modal-title">Connexion SSH</div>
          <div class="modal-sub">Équipement réseau via port 22</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="field">
        <label>Hôte</label>
        <input id="f-host" type="text" placeholder="192.168.1.1" autocomplete="off" spellcheck="false" />
      </div>
      <div class="field-row">
        <div class="field" style="width:120px">
          <label>Port</label>
          <input id="f-port" type="text" value="22" />
        </div>
        <div class="field" style="flex:1">
          <label>Utilisateur</label>
          <input id="f-user" type="text" placeholder="admin" autocomplete="off" spellcheck="false" />
        </div>
      </div>
      <div class="field">
        <label>Mot de passe</label>
        <input id="f-pass" type="password" autocomplete="off" />
      </div>
      <div class="modal-error" id="m-err"></div>
      <div class="modal-btns">
        <button class="btn btn-secondary" id="m-cancel">Annuler</button>
        <button class="btn btn-primary" id="m-ok">Connecter</button>
      </div>
    </div>
  `);

  const host = modal.querySelector("#f-host") as HTMLInputElement;
  const port = modal.querySelector("#f-port") as HTMLInputElement;
  const user = modal.querySelector("#f-user") as HTMLInputElement;
  const pass = modal.querySelector("#f-pass") as HTMLInputElement;
  const err = modal.querySelector("#m-err") as HTMLDivElement;
  const ok = modal.querySelector("#m-ok") as HTMLButtonElement;
  host.focus();

  async function submit() {
    if (!host.value.trim() || !user.value.trim()) {
      err.textContent = "Hôte et utilisateur sont requis.";
      return;
    }
    ok.disabled = true;
    ok.textContent = "Connexion…";
    err.textContent = "";
    try {
      await onConnect({
        host: host.value.trim(),
        port: parseInt(port.value, 10) || 22,
        username: user.value.trim(),
        password: pass.value,
      });
      closeModal();
    } catch (e) {
      err.textContent = String(e);
      ok.disabled = false;
      ok.textContent = "Connecter";
    }
  }

  ok.onclick = submit;
  (modal.querySelector("#m-cancel") as HTMLButtonElement).onclick = closeModal;
  modal.querySelectorAll("input").forEach((i) =>
    i.addEventListener("keydown", (e) => {
      if ((e as KeyboardEvent).key === "Enter") submit();
    })
  );
}

// ---------------------------------------------------------------------------
// Serial modal
// ---------------------------------------------------------------------------
const BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
const PARITIES = [
  { label: "None", value: 0 },
  { label: "Odd", value: 1 },
  { label: "Even", value: 2 },
];

export async function openSerialModal(onConnect: (p: SerialParams) => Promise<void>) {
  let ports: string[] = [];
  try {
    ports = await invoke<string[]>("list_serial_ports");
  } catch {
    ports = [];
  }

  const portOptions = ports.length
    ? ports.map((p) => `<option value="${p}">${p}</option>`).join("")
    : `<option value="">Aucun port détecté</option>`;

  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <span class="modal-badge modal-badge-com">COM</span>
        <div>
          <div class="modal-title">Connexion série</div>
          <div class="modal-sub">Console équipement via port COM</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="field">
        <label>Port COM</label>
        <select id="f-port">${portOptions}</select>
      </div>
      <div class="field-row">
        <div class="field" style="width:180px">
          <label>Baud rate</label>
          <select id="f-baud">
            ${BAUD_RATES.map((b) => `<option value="${b}"${b === 9600 ? " selected" : ""}>${b}</option>`).join("")}
          </select>
        </div>
        <div class="field" style="flex:1">
          <label>Data bits</label>
          <select id="f-data">
            ${[8, 7, 6, 5].map((d) => `<option value="${d}">${d}</option>`).join("")}
          </select>
        </div>
      </div>
      <div class="field-row">
        <div class="field" style="width:180px">
          <label>Parité</label>
          <select id="f-parity">
            ${PARITIES.map((p) => `<option value="${p.value}">${p.label}</option>`).join("")}
          </select>
        </div>
        <div class="field" style="flex:1">
          <label>Stop bits</label>
          <select id="f-stop">
            ${[1, 2].map((s) => `<option value="${s}">${s}</option>`).join("")}
          </select>
        </div>
      </div>
      <div class="modal-error" id="m-err"></div>
      <div class="modal-btns">
        <button class="btn btn-secondary" id="m-cancel">Annuler</button>
        <button class="btn btn-primary btn-success" id="m-ok">Connecter</button>
      </div>
    </div>
  `);

  const sel = (id: string) => modal.querySelector(id) as HTMLSelectElement;
  const portEl = sel("#f-port");
  const err = modal.querySelector("#m-err") as HTMLDivElement;
  const ok = modal.querySelector("#m-ok") as HTMLButtonElement;
  portEl.focus();

  async function submit() {
    if (!portEl.value) {
      err.textContent = "Aucun port COM disponible.";
      return;
    }
    ok.disabled = true;
    ok.textContent = "Connexion…";
    err.textContent = "";
    try {
      await onConnect({
        port: portEl.value,
        baudRate: parseInt(sel("#f-baud").value, 10),
        dataBits: parseInt(sel("#f-data").value, 10),
        parity: parseInt(sel("#f-parity").value, 10),
        stopBits: parseInt(sel("#f-stop").value, 10),
      });
      closeModal();
    } catch (e) {
      err.textContent = String(e);
      ok.disabled = false;
      ok.textContent = "Connecter";
    }
  }

  ok.onclick = submit;
  (modal.querySelector("#m-cancel") as HTMLButtonElement).onclick = closeModal;
}

// ---------------------------------------------------------------------------
// Settings modal — theme + font size, persists to localStorage.
// ---------------------------------------------------------------------------
export type ThemeChoice = "system" | "dark" | "light";

export interface Settings {
  theme: ThemeChoice;
  fontSize: number;
}

const SETTINGS_KEY = "netflow.settings.v1";
const DEFAULT_SETTINGS: Settings = { theme: "system", fontSize: 14 };

export function loadSettings(): Settings {
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (!raw) return { ...DEFAULT_SETTINGS };
    const obj = JSON.parse(raw);
    return {
      theme: ["system", "dark", "light"].includes(obj.theme) ? obj.theme : "system",
      fontSize:
        typeof obj.fontSize === "number" && obj.fontSize >= 8 && obj.fontSize <= 32
          ? obj.fontSize
          : 14,
    };
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

export function saveSettings(s: Settings) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
}

function effectiveTheme(choice: ThemeChoice): "dark" | "light" {
  if (choice === "system") {
    return window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
  }
  return choice;
}

export function applyTheme(choice: ThemeChoice) {
  document.documentElement.setAttribute("data-theme", effectiveTheme(choice));
}

// Re-apply when the OS color scheme changes (relevant only when theme = system).
window.matchMedia("(prefers-color-scheme: light)").addEventListener("change", () => {
  const s = loadSettings();
  if (s.theme === "system") applyTheme("system");
});

export function openSettingsModal(onChange: (s: Settings) => void) {
  const current = loadSettings();

  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <span class="modal-badge modal-badge-set">SET</span>
        <div>
          <div class="modal-title">Paramètres</div>
          <div class="modal-sub">Apparence et terminal</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="field">
        <label>Thème</label>
        <div class="seg" id="seg-theme">
          <button data-v="system">Système</button>
          <button data-v="dark">Sombre</button>
          <button data-v="light">Clair</button>
        </div>
      </div>
      <div class="field">
        <label>Taille police terminal</label>
        <div class="row-num">
          <button id="fs-minus">−</button>
          <input id="fs-val" type="text" readonly />
          <button id="fs-plus">+</button>
          <span style="color: var(--text-muted); font-size: 11px; margin-left: 8px">px</span>
        </div>
      </div>
      <div class="modal-btns">
        <button class="btn btn-primary" id="m-close">Fermer</button>
      </div>
    </div>
  `);

  const segButtons = modal.querySelectorAll<HTMLButtonElement>("#seg-theme button");
  const fsVal = modal.querySelector("#fs-val") as HTMLInputElement;

  function refresh() {
    segButtons.forEach((b) => b.classList.toggle("on", b.dataset.v === current.theme));
    fsVal.value = String(current.fontSize);
  }
  refresh();

  function commit() {
    saveSettings(current);
    applyTheme(current.theme);
    onChange(current);
  }

  segButtons.forEach((b) => {
    b.onclick = () => {
      current.theme = b.dataset.v as ThemeChoice;
      refresh();
      commit();
    };
  });
  (modal.querySelector("#fs-minus") as HTMLButtonElement).onclick = () => {
    current.fontSize = Math.max(8, current.fontSize - 1);
    refresh();
    commit();
  };
  (modal.querySelector("#fs-plus") as HTMLButtonElement).onclick = () => {
    current.fontSize = Math.min(32, current.fontSize + 1);
    refresh();
    commit();
  };
  (modal.querySelector("#m-close") as HTMLButtonElement).onclick = closeModal;
}
