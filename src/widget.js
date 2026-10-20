(function (version) {
  ['__bsc', '__fbs'].forEach(function (name) {
    const previous = window[name];
    if (previous && typeof previous.dispose === 'function') {
      try { previous.dispose(); } catch (error) { }
    }
  });

  const POPUP_PREFIX = 'valve.steam.gamepadui.barpopup.';
  const MAIN_OVERLAY_KEY = 'valve.steam.gamepadui.main';
  const WANT_TAB_MS = 4000;
  const STALE_MS = 20000;
  const PENDING_MS = 8000;
  const LONG_PRESS_MS = 600;
  const PRESS_SLOP_PX = 14;
  const SUPPRESS_CLICK_MS = 1500;
  const EDIT_CHECK_MS = 250;
  const NAME_MAX_CHARACTERS = 32;
  const APP_LAUNCHING = 1;
  const APP_RUNNING = 4;
  const OP = { SET: 'set', ALL_ON: 'all-on', ALL_OFF: 'all-off', RENAME: 'rename', OPTION: 'option' };
  const MODE = { NONE: 'none', STALE: 'stale', BUSY: 'busy', ERROR: 'error', BLUETOOTH_OFF: 'bt-off' };
  const TRANSIENT_CLASSES = ['Panel', 'Focusable', 'gpfocus', 'gpfocuswithin'];
  const LED = { on: '#59BF40', booting: '#FFC82C', busy: '#FFC82C', error: '#E5483B', off: '', missing: '' };
  const BODY = 'M11 5H25Q29 5 29 9V27Q29 31 25 31H11Q7 31 7 27V9Q7 5 11 5Z';
  const WINDOW = 'M18 7A4.5 4.5 0 1 0 18 16A4.5 4.5 0 1 0 18 7Z';
  const GEAR = '<svg viewBox="0 0 36 36" fill="none" xmlns="http://www.w3.org/2000/svg">' +
    '<circle cx="18" cy="18" r="12.5" stroke="currentColor" stroke-width="5" stroke-dasharray="4.91 4.91"/>' +
    '<circle cx="18" cy="18" r="8.5" stroke="currentColor" stroke-width="4.5"/></svg>';
  const CHECK = '<svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M3 8.5L6.5 12L13 4.5" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
  const CROSS = '<svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M4 4L12 12M12 4L4 12" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"/></svg>';

  const BAR_CSS = [
    '@keyframes bsc-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.25; } }',
    '#bsc-icon .bsc-pulse { animation: bsc-pulse 1.2s ease-in-out infinite; }',
    '#bsc-icon .bsc-led { transition: fill 0.3s; }',
    '#bsc-icon.bsc-dim { opacity: 0.55; }',
    '#bsc-icon svg { overflow: visible; }'
  ].join('\n');

  const POPUP_CSS = [
    '@keyframes bsc-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.25; } }',
    '@keyframes bsc-enter { from { opacity: 0; transform: translateY(12%); } to { opacity: 1; transform: translateY(0); } }',
    '@keyframes bsc-press { 0%, 25% { transform: scaleX(0); } 100% { transform: scaleX(1); } }',
    '@keyframes bsc-fade { from { opacity: 0; } to { opacity: 1; } }',
    '@keyframes bsc-pop { from { opacity: 0; transform: translateY(-8px) scale(0.98); } to { opacity: 1; transform: none; } }',
    '[data-bsc-hidden] { display: none !important; }',
    '#bsc-wrapper { flex-direction: column; animation: bsc-enter 320ms cubic-bezier(0, 0, 0.1, 1) 80ms both; }',
    '#bsc-header { display: flex; justify-content: space-between; align-items: center; padding-right: 12px; }',
    '.bsc-icon-button { flex: 0 0 auto; width: 36px; height: 36px; margin: 0; padding: 0; border: none; border-radius: 4px; background: transparent; color: #8B929A; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: background-color 0.15s, color 0.15s; }',
    '.bsc-icon-button:hover, .bsc-icon-button.bsc-open { background: rgba(255, 255, 255, 0.1); color: #FFFFFF; }',
    '.bsc-icon-button svg { width: 22px; height: 22px; display: block; overflow: visible; }',
    '#bsc-gear[hidden] { display: none; }',
    '#bsc-panel { flex: 1 1 1px; min-height: 0; display: flex; flex-direction: column; padding-top: 16px; }',
    '#bsc-list { flex: 1 1 auto; min-height: 0; overflow-y: auto; padding-bottom: 60px; }',
    '#bsc-list::-webkit-scrollbar { display: none; }',
    '.bsc-row { position: relative; overflow: hidden; display: flex; align-items: center; gap: 12px; height: 52px; padding: 0 16px; border-radius: 2px; cursor: pointer; transition: background-color 0.15s; }',
    '.bsc-row:hover { background: rgba(255, 255, 255, 0.07); }',
    '.bsc-row.bsc-disabled, .bsc-row.bsc-working, .bsc-row.bsc-editing { cursor: default; }',
    '.bsc-row.bsc-disabled:hover, .bsc-row.bsc-working:hover { background: transparent; }',
    '.bsc-row.bsc-pressing::after { content: ""; position: absolute; inset: 0; background: rgba(255, 255, 255, 0.1); transform-origin: left center; animation: bsc-press ' + LONG_PRESS_MS + 'ms linear forwards; pointer-events: none; }',
    '.bsc-row.bsc-editing { background: rgba(255, 255, 255, 0.07); }',
    '.bsc-row.bsc-editing .bsc-name, .bsc-row.bsc-editing .bsc-toggle { display: none; }',
    '.bsc-glyph { flex: 0 0 26px; width: 26px; height: 26px; color: #DCDEDF; }',
    '.bsc-glyph svg { width: 26px; height: 26px; display: block; overflow: visible; }',
    '.bsc-row.bsc-disabled .bsc-glyph { color: #67707B; }',
    '.bsc-led { transition: fill 0.3s; }',
    '.bsc-pulse { animation: bsc-pulse 1.2s ease-in-out infinite; }',
    '.bsc-text { flex: 1 1 auto; min-width: 0; display: flex; flex-direction: column; gap: 5px; }',
    '.bsc-name { color: #FFFFFF; font-size: 16px; line-height: 18px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }',
    '.bsc-row.bsc-disabled .bsc-name { color: #8B929A; }',
    '.bsc-name-input { box-sizing: border-box; width: 100%; height: 26px; margin: -4px 0 -3px -7px; padding: 0 6px; border: 1px solid #1A9FFF; border-radius: 3px; background: rgba(0, 0, 0, 0.35); color: #FFFFFF; font: inherit; font-size: 16px; line-height: 24px; outline: none; }',
    '.bsc-name-input::placeholder { color: #67707B; }',
    '.bsc-edit-actions { flex: 0 0 auto; display: flex; gap: 6px; }',
    '.bsc-edit-actions .bsc-icon-button { width: 32px; height: 32px; background: rgba(255, 255, 255, 0.1); color: #DCDEDF; }',
    '.bsc-edit-actions .bsc-icon-button svg { width: 16px; height: 16px; }',
    '.bsc-edit-actions .bsc-edit-save { background: #1A9FFF; color: #FFFFFF; }',
    '.bsc-edit-actions .bsc-edit-save:hover { background: #47B4FF; }',
    '.bsc-caption { color: #969696; font-size: 10px; font-weight: 700; line-height: 11px; text-transform: uppercase; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; transition: color 0.3s; }',
    '.bsc-caption.bsc-tone-busy { color: #FFC82C; }',
    '.bsc-caption.bsc-tone-on { color: #59BF40; }',
    '.bsc-caption.bsc-tone-error { color: #E5483B; }',
    '.bsc-toggle { position: relative; flex: 0 0 38px; width: 38px; height: 22px; border-radius: 16px; transition: box-shadow 50ms ease-out; }',
    '.bsc-row:not(.bsc-disabled):not(.bsc-working):hover .bsc-toggle, .bsc-option:hover .bsc-toggle { box-shadow: rgba(255, 255, 255, 0.3) 0 0 0 4px; transition: box-shadow 100ms ease-in; }',
    '.bsc-track { position: absolute; inset: 0; background: rgba(255, 255, 255, 0.15); border-radius: 9001px; overflow: hidden; }',
    '.bsc-track::before { content: ""; position: absolute; inset: 0; background: #1A9FFF; transform: translateX(-27px); transition: transform 0.2s ease-out; }',
    '.bsc-toggle.bsc-checked .bsc-track::before { transform: translateX(-11px); }',
    '.bsc-knob { position: absolute; left: 0; top: 0; width: 22px; height: 22px; border-radius: 9001px; background: #FFFFFF; box-shadow: rgba(0, 0, 0, 0.35) 0 0 5px 0; transition: transform 0.2s cubic-bezier(0.1, 0.12, 0.53, 1.72); }',
    '.bsc-toggle.bsc-checked .bsc-knob { transform: translateX(16px); }',
    '.bsc-toggle.bsc-busy .bsc-knob { animation: bsc-pulse 1s ease-in-out infinite; }',
    '.bsc-row.bsc-disabled .bsc-track { background: #67707B; }',
    '.bsc-row.bsc-disabled .bsc-track::before { opacity: 0; }',
    '.bsc-row.bsc-disabled .bsc-knob { background: #8B929A; }',
    '#bsc-empty { padding: 10px 16px; display: flex; flex-direction: column; gap: 6px; }',
    '#bsc-empty-title { color: #FFFFFF; font-size: 16px; }',
    '#bsc-empty-text { color: #8B929A; font-size: 13px; line-height: 18px; }',
    '#bsc-note { flex: 0 0 auto; padding: 0 16px; color: #8B929A; font-size: 12px; line-height: 16px; }',
    '#bsc-note.bsc-tone-error { color: #E5483B; }',
    '#bsc-actions { flex: 0 0 auto; display: flex; gap: 8px; padding: 0 16px 8px; }',
    '#bsc-actions button { flex: 1 1 0; min-width: 0; height: 36px; margin: 0; padding: 0 6px; font-size: 14px; line-height: 36px; white-space: nowrap; }',
    '#bsc-actions button:enabled { color: #FFFFFF; }',
    '[data-bsc-deselected] svg { transform: none !important; }',
    '#bsc-actions button[hidden] { display: none; }',
    '#bsc-options-backdrop { position: absolute; inset: 0; z-index: 20; background: rgba(0, 0, 0, 0.5); animation: bsc-fade 160ms ease-out; }',
    '#bsc-options { position: absolute; z-index: 21; left: 10px; right: 10px; padding: 4px 0 8px; overflow-y: auto; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 6px; background: #23262E; box-shadow: 0 10px 28px rgba(0, 0, 0, 0.6); transform-origin: top right; animation: bsc-pop 180ms cubic-bezier(0, 0, 0.1, 1); }',
    '#bsc-options::-webkit-scrollbar { display: none; }',
    '#bsc-options-head { display: flex; align-items: center; justify-content: space-between; padding: 2px 6px 0 16px; color: #FFFFFF; font-size: 18px; font-weight: 700; }',
    '.bsc-section { padding: 8px 16px 4px; color: #8B929A; font-size: 13px; font-weight: 600; line-height: 16px; }',
    '.bsc-option { display: flex; align-items: center; gap: 12px; min-height: 32px; padding: 2px 16px; cursor: pointer; transition: background-color 0.15s; }',
    '#bsc-options.bsc-compact .bsc-option { min-height: 28px; }',
    '#bsc-options.bsc-compact .bsc-section { padding-top: 6px; padding-bottom: 2px; }',
    '.bsc-option:hover { background: rgba(255, 255, 255, 0.07); }',
    '.bsc-option .bsc-text { gap: 3px; }',
    '.bsc-option-title { color: #FFFFFF; font-size: 15px; line-height: 18px; }',
    '.bsc-option-detail { color: #8B929A; font-size: 12px; line-height: 15px; }',
    '.bsc-option-detail:empty { display: none; }',
    '.bsc-option-detail.bsc-tone-error { color: #E5483B; }'
  ].join('\n');

  const st = {
    version: version,
    session: Math.random().toString(36).slice(2, 10),
    seq: 0,
    data: null,
    lastData: 0,
    queue: [],
    pending: new Map(),
    optionPending: new Map(),
    renamePending: new Map(),
    popupWindow: null,
    popupDoc: null,
    popupObserver: null,
    popupOpen: false,
    tabActive: false,
    wantTab: false,
    wantTabAt: 0,
    classes: null,
    textClass: '',
    drawnIcon: '',
    drawnPanel: '',
    drawnOptions: '',
    optionsOpen: false,
    press: null,
    suppressClick: null,
    edit: null,
    keyHookManager: null,
    keyHookPrevious: null,
    syncing: false,
    wasStale: false,
    timer: 0,
    reportedApps: ''
  };

  function glyph(filled, led, pulse) {
    let svg = '<svg viewBox="0 0 36 36" fill="none" xmlns="http://www.w3.org/2000/svg">';
    if (filled) {
      svg += '<path d="' + BODY + ' ' + WINDOW + '" fill="currentColor" fill-rule="evenodd"/>';
    } else {
      svg += '<path d="' + BODY + '" stroke="currentColor" stroke-width="2.5"/>' +
        '<circle cx="18" cy="11.5" r="3.4" stroke="currentColor" stroke-width="2"/>';
    }
    if (led) {
      svg += '<circle class="bsc-led' + (pulse ? ' bsc-pulse' : '') + '" cx="18" cy="11.5" r="' + (filled ? '3' : '2.2') + '" fill="' + led + '"/>';
    }
    return svg + '</svg>';
  }

  function notify(reason) {
    try {
      if (typeof window.__bscNotify === 'function') window.__bscNotify(String(reason));
    } catch (error) { }
  }

  function isStale() {
    return !st.data || Date.now() - st.lastData > STALE_MS;
  }

  function fresh(map, key) {
    const entry = map.get(key);
    return entry && Date.now() - entry.at <= PENDING_MS ? entry : null;
  }

  function prunePending() {
    const now = Date.now();
    let pruned = false;
    [st.pending, st.optionPending, st.renamePending].forEach(function (map) {
      map.forEach(function (entry, key) {
        if (now - entry.at > PENDING_MS) {
          map.delete(key);
          pruned = true;
        }
      });
    });
    return pruned;
  }

  function viewData() {
    const data = st.data || {};
    const stale = isStale();
    const stations = (data.stations || []).map(function (station) {
      let shown = station;
      const renamed = fresh(st.renamePending, station.id);
      if (renamed) {
        shown = Object.assign({}, shown, { title: renamed.title, nickname: renamed.nickname });
      }
      const pending = fresh(st.pending, station.id);
      if (pending) {
        shown = Object.assign({}, shown, {
          on: pending.on,
          busy: true,
          led: 'busy',
          tone: 'busy',
          caption: pending.on ? 'Starting…' : 'Shutting down…'
        });
      }
      if (stale) shown = Object.assign({}, shown, { enabled: false });
      return shown;
    });
    let icon = Object.assign({ mode: MODE.NONE, count: 0, label: 'Base stations' }, data.icon || {});
    if (stale) {
      icon = { mode: MODE.STALE, count: 0, label: 'Base stations: service not responding' };
    } else if (st.pending.size && icon.mode !== MODE.BLUETOOTH_OFF) {
      icon = Object.assign({}, icon, { mode: MODE.BUSY });
    }
    let options = null;
    if (!stale && data.options && Array.isArray(data.options.items)) {
      options = data.options.items.map(function (item) {
        const pending = fresh(st.optionPending, item.key);
        return pending ? Object.assign({}, item, { on: pending.on, detail: item.tone === 'error' ? '' : item.detail, tone: '' }) : item;
      });
    }
    return {
      stale: stale,
      stations: stations,
      icon: icon,
      empty: stale ? { title: 'Not responding', text: 'The base station service stopped responding.' } : (data.empty || null),
      note: stale ? '' : (data.note || ''),
      noteTone: data.noteTone || '',
      allOn: !stale && !!(data.allOn && data.allOn.enabled),
      allOff: !stale && !!(data.allOff && data.allOff.enabled),
      showAll: !stale && stations.length > 0 && !data.extra,
      extra: stale ? null : (data.extra || null),
      options: options
    };
  }

  function findPopupWindow() {
    try {
      const manager = window.opener && window.opener.g_PopupManager;
      const popups = manager && manager.m_mapPopups;
      if (!popups || typeof popups.forEach !== 'function') return null;
      let found = null;
      popups.forEach(function (popup, key) {
        if (found || !popup) return;
        const name = String(popup.m_strName || key || '');
        const win = popup.window || popup.m_popup;
        if (name.indexOf(POPUP_PREFIX) === 0 && win && !win.closed && win.document) found = win;
      });
      return found;
    } catch (error) {
      return null;
    }
  }

  function ensureBarStyle() {
    if (document.getElementById('bsc-bar-style')) return;
    const style = document.createElement('style');
    style.id = 'bsc-bar-style';
    style.textContent = BAR_CSS;
    (document.head || document.documentElement).appendChild(style);
  }

  function ensureIcon() {
    const anchor = document.querySelector('[aria-label^="Headset Battery"]');
    let icon = document.getElementById('bsc-icon');
    if (!anchor || !anchor.parentElement) {
      if (icon) icon.remove();
      st.drawnIcon = '';
      return 'no-anchor';
    }
    ensureBarStyle();
    const row = anchor.parentElement;
    const bank = document.getElementById('fbm-powerbank');
    const next = bank && bank.parentElement === row ? bank : anchor;
    const percent = anchor.querySelector('span[class]') || Array.prototype.filter.call(row.querySelectorAll('span[class]'), function (span) {
      return !span.closest('#bsc-icon') && span.className.indexOf(' ') > 0;
    })[0];
    if (percent && percent.className) window.__bscTextClass = percent.className;
    st.textClass = window.__bscTextClass || '';
    if (!icon) {
      icon = document.createElement('div');
      icon.id = 'bsc-icon';
      icon.style.cursor = 'pointer';
      icon.addEventListener('click', onIconClick);
      st.drawnIcon = '';
    }
    const classes = Array.prototype.filter.call(anchor.classList, function (c) { return TRANSIENT_CLASSES.indexOf(c) < 0; }).join(' ');
    const wanted = classes + (icon.classList.contains('bsc-dim') ? ' bsc-dim' : '');
    if (icon.className !== wanted) icon.className = wanted;
    if (icon.parentElement !== row || icon.nextElementSibling !== next) row.insertBefore(icon, next);
    renderIcon(icon);
    return 'shown';
  }

  function renderIcon(icon) {
    const view = viewData().icon;
    const signature = JSON.stringify([view, st.textClass]);
    if (signature === st.drawnIcon && icon.firstChild) return;
    st.drawnIcon = signature;
    const mode = view.mode;
    const count = Math.max(0, view.count | 0);
    const filled = count > 0 && mode !== MODE.STALE && mode !== MODE.BLUETOOTH_OFF;
    const led = mode === MODE.ERROR ? LED.error : mode === MODE.BUSY ? LED.busy : filled ? LED.on : '';
    const text = filled ? String(count) : '';
    let label = '<span></span>';
    if (text) {
      label = st.textClass ? '<span class="' + st.textClass + '"></span>' : '<span style="align-self:center;font-size:15px;font-weight:600;padding-right:4px;font-variant-numeric:tabular-nums"></span>';
    }
    icon.innerHTML = label + glyph(filled, led, mode === MODE.BUSY);
    icon.firstChild.textContent = text;
    icon.classList.toggle('bsc-dim', mode === MODE.STALE || mode === MODE.BLUETOOTH_OFF || mode === MODE.NONE);
    icon.setAttribute('aria-label', view.label || 'Base stations');
  }

  function learnClasses(doc) {
    const tabs = Array.prototype.filter.call(doc.querySelectorAll('[role="tab"]'), function (t) { return t.id !== 'bsc-tab'; });
    const selected = tabs.filter(function (t) { return t.getAttribute('aria-selected') === 'true'; })[0];
    if (!tabs.length || !selected) return null;
    const keep = function (c) { return TRANSIENT_CLASSES.indexOf(c) < 0; };
    const common = Array.prototype.filter.call(tabs[0].classList, function (c) {
      return keep(c) && tabs.every(function (t) { return t.classList.contains(c); });
    });
    const tabSelected = Array.prototype.filter.call(selected.classList, function (c) { return keep(c) && common.indexOf(c) < 0; });
    const panel = doc.getElementById(selected.getAttribute('aria-controls') || '');
    const wrapper = panel && panel.parentElement;
    const header = wrapper && wrapper.firstElementChild !== panel ? wrapper.firstElementChild : null;
    if (!wrapper || !header || !wrapper.parentElement) return null;
    const unselected = tabs.filter(function (t) { return t !== selected; })[0];
    const view = doc.defaultView || window;
    const gearTab = doc.querySelector('[role="tab"][aria-label="Quick Settings"] svg');
    return {
      tab: common,
      tabSelected: tabSelected,
      wrapper: Array.prototype.filter.call(wrapper.classList, keep),
      header: Array.prototype.filter.call(header.classList, keep),
      unselectedColor: unselected ? view.getComputedStyle(unselected).color : 'rgb(139, 146, 154)',
      gear: gearTab ? gearTab.outerHTML : GEAR
    };
  }

  function ensurePopupStyle(doc) {
    let style = doc.getElementById('bsc-popup-style');
    const css = POPUP_CSS + '\n[data-bsc-deselected] { background: transparent !important; color: ' + st.classes.unselectedColor + ' !important; }';
    if (!style) {
      style = doc.createElement('style');
      style.id = 'bsc-popup-style';
      (doc.head || doc.documentElement).appendChild(style);
    }
    if (style.textContent !== css) style.textContent = css;
  }

  function ensureTab(doc) {
    const list = doc.querySelector('[role="tablist"]');
    if (!list) return null;
    let tab = doc.getElementById('bsc-tab');
    if (!tab) {
      tab = doc.createElement('div');
      tab.id = 'bsc-tab';
      tab.setAttribute('role', 'tab');
      tab.setAttribute('aria-label', 'Base Stations');
      tab.setAttribute('aria-controls', 'bsc-panel');
      tab.innerHTML = glyph(true, '', false).replace('<svg ', '<svg width="24" height="24" ');
      tab.addEventListener('click', function (event) {
        event.stopPropagation();
        if (!st.tabActive) {
          st.tabActive = true;
          syncPopup();
          notify('tab');
        }
      });
    }
    const wanted = st.classes.tab.concat(st.tabActive ? st.classes.tabSelected : []).join(' ');
    if (tab.className !== wanted) tab.className = wanted;
    const battery = list.querySelector('[aria-label="Battery Info"]');
    if (battery && battery.parentElement === list) {
      if (tab.parentElement !== list || tab.previousElementSibling !== battery) battery.after(tab);
    } else if (tab.parentElement !== list || list.lastElementChild !== tab) {
      list.appendChild(tab);
    }
    return tab;
  }

  function iconButton(doc, id, className, label, svg) {
    const button = doc.createElement('button');
    if (id) button.id = id;
    button.type = 'button';
    button.className = 'bsc-icon-button' + (className ? ' ' + className : '');
    button.setAttribute('aria-label', label);
    button.innerHTML = svg;
    return button;
  }

  function ensureWrapper(doc) {
    const steamPanel = doc.querySelector('[role="tabpanel"]:not(#bsc-panel)');
    const container = steamPanel && steamPanel.parentElement && steamPanel.parentElement.parentElement;
    if (!container) return null;
    let wrapper = doc.getElementById('bsc-wrapper');
    if (!wrapper) {
      wrapper = doc.createElement('div');
      wrapper.id = 'bsc-wrapper';
      const header = doc.createElement('div');
      header.id = 'bsc-header';
      const title = doc.createElement('span');
      title.textContent = 'Base Stations';
      const gear = iconButton(doc, 'bsc-gear', '', 'Options', st.classes.gear);
      gear.setAttribute('aria-haspopup', 'dialog');
      gear.addEventListener('click', onGearClick);
      header.appendChild(title);
      header.appendChild(gear);
      const panel = doc.createElement('div');
      panel.id = 'bsc-panel';
      panel.setAttribute('role', 'tabpanel');
      panel.addEventListener('click', onPanelClick);
      panel.addEventListener('pointerdown', onPanelPointerDown);
      panel.addEventListener('pointermove', onPanelPointerMove);
      panel.addEventListener('pointerup', cancelPress);
      panel.addEventListener('pointercancel', cancelPress);
      panel.addEventListener('pointerleave', cancelPress);
      panel.addEventListener('contextmenu', onPanelContextMenu);
      wrapper.appendChild(header);
      wrapper.appendChild(panel);
      st.drawnPanel = '';
      st.drawnOptions = '';
    }
    const wanted = st.classes.wrapper.join(' ');
    if (wrapper.className !== wanted) wrapper.className = wanted;
    const headerClass = st.classes.header.join(' ');
    if (wrapper.firstElementChild.className !== headerClass) wrapper.firstElementChild.className = headerClass;
    if (wrapper.parentElement !== container) container.appendChild(wrapper);
    const view = doc.defaultView || window;
    if (!wrapper.style.position && view.getComputedStyle(wrapper).position === 'static') wrapper.style.position = 'relative';
    return wrapper;
  }

  function setFlag(element, name, on) {
    if (on && !element.hasAttribute(name)) element.setAttribute(name, '');
    if (!on && element.hasAttribute(name)) element.removeAttribute(name);
  }

  function applyTabState(doc, tab, wrapper) {
    const active = st.tabActive;
    Array.prototype.forEach.call(wrapper.parentElement.children, function (child) {
      if (child !== wrapper) setFlag(child, 'data-bsc-hidden', active);
    });
    const display = active ? '' : 'none';
    if (wrapper.style.display !== display) wrapper.style.display = display;
    const selected = active ? 'true' : 'false';
    if (tab.getAttribute('aria-selected') !== selected) tab.setAttribute('aria-selected', selected);
    Array.prototype.forEach.call(doc.querySelectorAll('[role="tab"]'), function (other) {
      if (other === tab) return;
      const highlighted = other.getAttribute('aria-selected') === 'true' || other.classList.contains('gpfocus');
      setFlag(other, 'data-bsc-deselected', active && highlighted);
    });
  }

  function syncPopup() {
    if (st.syncing) return;
    st.syncing = true;
    try {
      const doc = st.popupDoc;
      const menu = doc && doc.getElementById('QuickAccess-Menu');
      if (!menu) {
        if (st.popupOpen) {
          st.popupOpen = false;
          st.tabActive = false;
          st.optionsOpen = false;
          notify('popup-closed');
        }
        return;
      }
      if (!st.popupOpen) {
        st.popupOpen = true;
        notify('popup-opened');
      }
      if (!st.classes) st.classes = learnClasses(doc);
      if (!st.classes) return;
      ensurePopupStyle(doc);
      const tab = ensureTab(doc);
      const wrapper = ensureWrapper(doc);
      if (!tab || !wrapper) return;
      if (st.wantTab) {
        st.wantTab = false;
        if (Date.now() - st.wantTabAt < WANT_TAB_MS) {
          st.tabActive = true;
          notify('tab');
        }
      }
      if (!st.tabActive) st.optionsOpen = false;
      const tabClass = st.classes.tab.concat(st.tabActive ? st.classes.tabSelected : []).join(' ');
      if (tab.className !== tabClass) tab.className = tabClass;
      applyTabState(doc, tab, wrapper);
      renderPanel(doc);
      renderOptions(doc);
    } finally {
      st.syncing = false;
    }
  }

  function onPopupClickCapture(event) {
    const target = event.target;
    const tab = target && target.closest ? target.closest('[role="tab"]') : null;
    if (tab && tab.id !== 'bsc-tab' && st.tabActive) {
      st.tabActive = false;
      syncPopup();
      notify('tab');
    }
  }

  function onPopupPointerDownCapture(event) {
    const edit = st.edit;
    if (!edit) return;
    const target = event.target;
    const row = edit.input.closest('.bsc-row');
    if (row && target && row.contains(target)) return;
    finishEdit(true);
  }

  function detachPopup() {
    finishEdit(false);
    cancelPress();
    if (st.popupObserver) {
      try { st.popupObserver.disconnect(); } catch (error) { }
    }
    const doc = st.popupDoc;
    if (doc) {
      try {
        doc.removeEventListener('click', onPopupClickCapture, true);
        doc.removeEventListener('pointerdown', onPopupPointerDownCapture, true);
        ['bsc-tab', 'bsc-wrapper', 'bsc-popup-style'].forEach(function (id) {
          const element = doc.getElementById(id);
          if (element) element.remove();
        });
        Array.prototype.forEach.call(doc.querySelectorAll('[data-bsc-hidden],[data-bsc-deselected]'), function (element) {
          element.removeAttribute('data-bsc-hidden');
          element.removeAttribute('data-bsc-deselected');
        });
      } catch (error) { }
    }
    st.popupObserver = null;
    st.popupDoc = null;
    st.popupWindow = null;
    st.classes = null;
    st.popupOpen = false;
    st.tabActive = false;
    st.optionsOpen = false;
    st.drawnPanel = '';
    st.drawnOptions = '';
  }

  function attachPopup() {
    const win = findPopupWindow();
    const doc = win ? win.document : null;
    if (doc && doc === st.popupDoc) return;
    detachPopup();
    if (!doc || !doc.body) return;
    st.popupWindow = win;
    st.popupDoc = doc;
    const Observer = win.MutationObserver || MutationObserver;
    st.popupObserver = new Observer(function () { syncPopup(); });
    st.popupObserver.observe(doc.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'aria-selected'] });
    doc.addEventListener('click', onPopupClickCapture, true);
    doc.addEventListener('pointerdown', onPopupPointerDownCapture, true);
    syncPopup();
  }

  function onIconClick(event) {
    attachPopup();
    const doc = st.popupDoc;
    const open = !!(doc && doc.getElementById('QuickAccess-Menu'));
    if (open && !st.tabActive) {
      event.stopPropagation();
      st.tabActive = true;
      syncPopup();
      notify('tab');
      return;
    }
    if (!open) {
      st.wantTab = true;
      st.wantTabAt = Date.now();
    }
    notify('icon');
  }

  function onGearClick(event) {
    event.stopPropagation();
    st.optionsOpen = !st.optionsOpen;
    renderEverything();
  }

  function stationById(id) {
    return viewData().stations.filter(function (s) { return s.id === id; })[0] || null;
  }

  function queueAction(action) {
    action.session = st.session;
    action.seq = ++st.seq;
    st.queue.push(action);
    const now = Date.now();
    if (action.op === OP.SET) {
      st.pending.set(action.id, { on: action.on, at: now, seq: action.seq });
    } else if (action.op === OP.ALL_ON || action.op === OP.ALL_OFF) {
      const on = action.op === OP.ALL_ON;
      viewData().stations.forEach(function (s) {
        if (s.enabled && !s.busy && s.on !== on) st.pending.set(s.id, { on: on, at: now, seq: action.seq });
      });
    } else if (action.op === OP.OPTION) {
      st.optionPending.set(action.key, { on: action.on, at: now, seq: action.seq });
    } else if (action.op === OP.RENAME && action.title) {
      st.renamePending.set(action.id, { title: action.title, nickname: action.name, at: now, seq: action.seq });
      delete action.title;
    }
    renderEverything();
    notify('action');
  }

  function onPanelClick(event) {
    const target = event.target;
    if (!target || !target.closest) return;
    const suppressed = st.suppressClick;
    if (suppressed) {
      st.suppressClick = null;
      if (Date.now() < suppressed.until) return;
    }
    const edit = st.edit;
    if (edit && edit.input.closest('.bsc-row') && edit.input.closest('.bsc-row').contains(target)) {
      if (target.closest('.bsc-edit-save')) finishEdit(true);
      else if (target.closest('.bsc-edit-cancel')) finishEdit(false);
      else if (target === edit.input) showKeyboard();
      return;
    }
    if (isStale()) return;
    const button = target.closest('[data-bsc-op]');
    if (button) {
      if (!button.disabled) queueAction({ op: button.getAttribute('data-bsc-op') });
      return;
    }
    const row = target.closest('[data-bsc-id]');
    if (!row || row.classList.contains('bsc-disabled')) return;
    const station = stationById(row.getAttribute('data-bsc-id'));
    if (!station || !station.enabled || station.busy) return;
    queueAction({ op: OP.SET, id: station.id, on: !station.on });
  }

  function onPanelPointerDown(event) {
    if (event.button !== 0 || isStale()) return;
    const target = event.target;
    const row = target && target.closest ? target.closest('.bsc-row') : null;
    cancelPress();
    if (!row || row.classList.contains('bsc-editing')) return;
    st.press = {
      id: row.getAttribute('data-bsc-id'),
      row: row,
      x: event.clientX,
      y: event.clientY,
      timer: setTimeout(onLongPress, LONG_PRESS_MS)
    };
    row.classList.add('bsc-pressing');
  }

  function onPanelPointerMove(event) {
    const press = st.press;
    if (press && Math.hypot(event.clientX - press.x, event.clientY - press.y) > PRESS_SLOP_PX) cancelPress();
  }

  function cancelPress() {
    const press = st.press;
    if (!press) return;
    st.press = null;
    clearTimeout(press.timer);
    press.row.classList.remove('bsc-pressing');
  }

  function onLongPress() {
    const press = st.press;
    if (!press) return;
    st.press = null;
    press.row.classList.remove('bsc-pressing');
    st.suppressClick = { until: Date.now() + SUPPRESS_CLICK_MS };
    startEdit(press.id);
  }

  function onPanelContextMenu(event) {
    const target = event.target;
    const row = target && target.closest ? target.closest('.bsc-row') : null;
    if (!row || isStale()) return;
    event.preventDefault();
    cancelPress();
    startEdit(row.getAttribute('data-bsc-id'));
  }

  function characterCount(text) {
    return Array.from(text).length;
  }

  function isHighSurrogate(code) {
    return code >= 0xD800 && code <= 0xDBFF;
  }

  function isLowSurrogate(code) {
    return code >= 0xDC00 && code <= 0xDFFF;
  }

  function cleanName(text) {
    return String(text).replace(/[\u0000-\u001f\u007f"\\]/g, '');
  }

  function startEdit(id) {
    if (st.edit) finishEdit(st.edit.id !== id);
    const doc = st.popupDoc;
    const station = stationById(id);
    const row = doc && Array.prototype.filter.call(doc.querySelectorAll('.bsc-row'), function (r) {
      return r.getAttribute('data-bsc-id') === id;
    })[0];
    if (!row || !station) return;
    const name = row.querySelector('.bsc-name');
    const input = doc.createElement('input');
    input.type = 'text';
    input.className = 'bsc-name-input';
    input.spellcheck = false;
    input.autocomplete = 'off';
    input.setAttribute('aria-label', 'Name of ' + station.title);
    input.placeholder = station.defaultTitle || station.title;
    input.value = station.nickname || station.title;
    input.addEventListener('keydown', onEditKeyDown);
    input.addEventListener('beforeinput', onEditBeforeInput);
    const actions = doc.createElement('div');
    actions.className = 'bsc-edit-actions';
    actions.appendChild(iconButton(doc, '', 'bsc-edit-cancel', 'Cancel', CROSS));
    actions.appendChild(iconButton(doc, '', 'bsc-edit-save', 'Save', CHECK));
    name.after(input);
    row.appendChild(actions);
    row.classList.add('bsc-editing');
    st.edit = { id: id, input: input, keyboard: false, timer: setInterval(maintainEdit, EDIT_CHECK_MS) };
    try {
      input.focus();
      input.select();
    } catch (error) { }
    showKeyboard();
    notify('edit');
  }

  function editAlive() {
    const edit = st.edit;
    return !!(edit && edit.input.isConnected && st.popupOpen && st.tabActive && st.popupDoc &&
      st.popupDoc.getElementById('QuickAccess-Menu'));
  }

  function maintainEdit() {
    if (!st.edit) return;
    if (!editAlive()) {
      finishEdit(true);
      return;
    }
    if (st.edit.keyboard) hookKeyboard();
  }

  function finishEdit(save) {
    const edit = st.edit;
    if (!edit) return;
    st.edit = null;
    clearInterval(edit.timer);
    unhookKeyboard();
    if (edit.keyboard) hideKeyboard();
    const value = edit.input.value.trim();
    const row = edit.input.closest('.bsc-row');
    edit.input.remove();
    if (row) {
      row.classList.remove('bsc-editing');
      const actions = row.querySelector('.bsc-edit-actions');
      if (actions) actions.remove();
    }
    if (save) {
      const station = stationById(edit.id);
      if (station) {
        const fallback = station.defaultTitle || '';
        const nickname = value === fallback ? '' : value;
        if (nickname !== (station.nickname || '')) {
          queueAction({ op: OP.RENAME, id: edit.id, name: nickname, title: nickname || fallback || station.title });
          return;
        }
      }
    }
    st.drawnPanel = '';
    renderEverything();
  }

  function onEditKeyDown(event) {
    if (event.key === 'Enter') {
      event.preventDefault();
      finishEdit(true);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      finishEdit(false);
    }
    event.stopPropagation();
  }

  function onEditBeforeInput(event) {
    if (typeof event.data !== 'string' || !event.data) return;
    const input = event.target;
    const data = cleanName(event.data);
    const start = input.selectionStart == null ? input.value.length : input.selectionStart;
    const end = input.selectionEnd == null ? input.value.length : input.selectionEnd;
    const next = input.value.slice(0, start) + data + input.value.slice(end);
    if (data !== event.data || characterCount(next) > NAME_MAX_CHARACTERS) event.preventDefault();
  }

  function typeKey(key) {
    const input = st.edit.input;
    const value = input.value;
    let start = input.selectionStart == null ? value.length : input.selectionStart;
    const end = input.selectionEnd == null ? value.length : input.selectionEnd;
    if (key === 'Enter' || key === '\n' || key === '\r') {
      finishEdit(true);
      return;
    }
    let next = value;
    let caret = start;
    if (key === 'Backspace' || key === '\b') {
      if (start === end && start > 0) {
        start -= 1;
        if (start > 0 && isLowSurrogate(value.charCodeAt(start)) && isHighSurrogate(value.charCodeAt(start - 1))) start -= 1;
      }
      next = value.slice(0, start) + value.slice(end);
      caret = start;
    } else if (key === 'ArrowLeft') {
      caret = start;
      if (start === end && caret > 0) {
        caret -= 1;
        if (caret > 0 && isLowSurrogate(value.charCodeAt(caret)) && isHighSurrogate(value.charCodeAt(caret - 1))) caret -= 1;
      }
    } else if (key === 'ArrowRight') {
      caret = end;
      if (start === end && caret < value.length) {
        caret += 1;
        if (caret < value.length && isLowSurrogate(value.charCodeAt(caret)) && isHighSurrogate(value.charCodeAt(caret - 1))) caret += 1;
      }
    } else {
      const text = cleanName(key.length > 1 && /^[A-Z][a-zA-Z]+$/.test(key) ? '' : key);
      if (!text) return;
      next = value.slice(0, start) + text + value.slice(end);
      if (characterCount(next) > NAME_MAX_CHARACTERS) return;
      caret = start + text.length;
    }
    input.value = next;
    try { input.setSelectionRange(caret, caret); } catch (error) { }
  }

  function keyboardManager() {
    try {
      const store = window.opener && window.opener.SteamUIStore && window.opener.SteamUIStore.WindowStore;
      const instances = store && store.SteamUIWindows;
      if (!instances || typeof instances.length !== 'number') return null;
      let found = null;
      Array.prototype.forEach.call(instances, function (instance) {
        const manager = instance && instance.VirtualKeyboardManager;
        if (!manager || typeof manager.SetVRKeyboardOnTextEnteredOverride !== 'function') return;
        let key = '';
        try { key = String(instance.GetVROverlayKey ? instance.GetVROverlayKey() : ''); } catch (error) { }
        if (!found || key === MAIN_OVERLAY_KEY) found = manager;
      });
      return found;
    } catch (error) {
      return null;
    }
  }

  function onVirtualKey(key) {
    if (!editAlive()) {
      const previous = st.keyHookPrevious;
      unhookKeyboard();
      if (typeof previous === 'function') previous(key);
      return;
    }
    typeKey(String(key));
  }

  function hookKeyboard() {
    const manager = keyboardManager();
    if (!manager) return false;
    if (st.keyHookManager && st.keyHookManager !== manager) unhookKeyboard();
    if (manager.m_fnVROnTextEnteredOverride === onVirtualKey) return true;
    st.keyHookPrevious = manager.m_fnVROnTextEnteredOverride || null;
    st.keyHookManager = manager;
    try {
      manager.SetVRKeyboardOnTextEnteredOverride(onVirtualKey);
    } catch (error) {
      st.keyHookManager = null;
      st.keyHookPrevious = null;
      return false;
    }
    return true;
  }

  function unhookKeyboard() {
    const manager = st.keyHookManager;
    if (manager) {
      try {
        if (manager.m_fnVROnTextEnteredOverride === onVirtualKey) manager.SetVRKeyboardOnTextEnteredOverride(st.keyHookPrevious);
      } catch (error) { }
    }
    st.keyHookManager = null;
    st.keyHookPrevious = null;
  }

  function vrKeyboard() {
    try {
      const client = st.popupWindow && st.popupWindow.SteamClient;
      const keyboard = client && client.OpenVR && client.OpenVR.Keyboard;
      return keyboard && typeof keyboard.Show === 'function' ? keyboard : null;
    } catch (error) {
      return null;
    }
  }

  function showKeyboard() {
    const edit = st.edit;
    const keyboard = vrKeyboard();
    if (!edit || !keyboard) return;
    hookKeyboard();
    try {
      keyboard.Show();
      edit.keyboard = true;
    } catch (error) { }
  }

  function hideKeyboard() {
    const keyboard = vrKeyboard();
    if (!keyboard || typeof keyboard.Hide !== 'function') return;
    try { keyboard.Hide(); } catch (error) { }
  }

  function onOptionsClick(event) {
    const target = event.target;
    if (!target || !target.closest) return;
    event.stopPropagation();
    if (target.closest('#bsc-options-close')) {
      st.optionsOpen = false;
      renderEverything();
      return;
    }
    const row = target.closest('[data-bsc-option]');
    if (!row) return;
    const key = row.getAttribute('data-bsc-option');
    const item = (viewData().options || []).filter(function (o) { return o.key === key; })[0];
    if (item) queueAction({ op: OP.OPTION, key: key, on: !item.on });
  }

  function onBackdropClick(event) {
    event.stopPropagation();
    st.optionsOpen = false;
    renderEverything();
  }

  function element(doc, tag, className, id) {
    const node = doc.createElement(tag);
    if (className) node.className = className;
    if (id) node.id = id;
    return node;
  }

  function makeToggle(doc) {
    const toggle = element(doc, 'div', 'bsc-toggle');
    toggle.setAttribute('role', 'switch');
    toggle.appendChild(element(doc, 'div', 'bsc-track'));
    toggle.appendChild(element(doc, 'div', 'bsc-knob'));
    return toggle;
  }

  function setToggle(toggle, on, busy, label) {
    toggle.classList.toggle('bsc-checked', !!on);
    toggle.classList.toggle('bsc-busy', !!busy);
    const checked = on ? 'true' : 'false';
    if (toggle.getAttribute('aria-checked') !== checked) toggle.setAttribute('aria-checked', checked);
    if (toggle.getAttribute('aria-label') !== label) toggle.setAttribute('aria-label', label);
  }

  function makeRow(doc) {
    const row = element(doc, 'div', 'bsc-row');
    row.appendChild(element(doc, 'div', 'bsc-glyph'));
    const text = element(doc, 'div', 'bsc-text');
    text.appendChild(element(doc, 'div', 'bsc-name'));
    text.appendChild(element(doc, 'div', 'bsc-caption'));
    row.appendChild(text);
    row.appendChild(makeToggle(doc));
    return row;
  }

  function updateRow(row, station) {
    const enabled = !!station.enabled;
    if (row.getAttribute('data-bsc-id') !== station.id) row.setAttribute('data-bsc-id', station.id);
    row.classList.toggle('bsc-disabled', !enabled);
    row.classList.toggle('bsc-working', enabled && !!station.busy);
    const glyphHtml = glyph(station.led !== 'missing' && station.led !== 'off', LED[station.led] || '', station.led === 'busy' || station.led === 'booting');
    const glyphBox = row.querySelector('.bsc-glyph');
    if (glyphBox.getAttribute('data-bsc-glyph') !== glyphHtml) {
      glyphBox.innerHTML = glyphHtml;
      glyphBox.setAttribute('data-bsc-glyph', glyphHtml);
    }
    const name = row.querySelector('.bsc-name');
    const caption = row.querySelector('.bsc-caption');
    if (name.textContent !== station.title) name.textContent = station.title;
    if (caption.textContent !== station.caption) caption.textContent = station.caption;
    const captionClass = 'bsc-caption' + (station.tone ? ' bsc-tone-' + station.tone : '');
    if (caption.className !== captionClass) caption.className = captionClass;
    setToggle(row.querySelector('.bsc-toggle'), station.on, station.busy, station.title);
  }

  function renderPanel(doc) {
    const panel = doc.getElementById('bsc-panel');
    const gear = doc.getElementById('bsc-gear');
    if (!panel || !gear) return;
    const view = viewData();
    gear.hidden = !view.options;
    gear.classList.toggle('bsc-open', st.optionsOpen && !!view.options);
    const signature = JSON.stringify([view.stations, view.empty, view.note, view.noteTone, view.allOn, view.allOff, view.showAll, view.extra]);
    if (signature === st.drawnPanel && panel.firstChild) return;
    st.drawnPanel = signature;

    let list = doc.getElementById('bsc-list');
    let empty = doc.getElementById('bsc-empty');
    let note = doc.getElementById('bsc-note');
    let actions = doc.getElementById('bsc-actions');
    if (!list) {
      list = element(doc, 'div', '', 'bsc-list');
      empty = element(doc, 'div', '', 'bsc-empty');
      empty.appendChild(element(doc, 'div', '', 'bsc-empty-title'));
      empty.appendChild(element(doc, 'div', '', 'bsc-empty-text'));
      list.appendChild(empty);
      note = element(doc, 'div', '', 'bsc-note');
      actions = element(doc, 'div', '', 'bsc-actions');
      const extra = element(doc, 'button', 'DialogButton', 'bsc-extra');
      const allOn = element(doc, 'button', 'DialogButton', 'bsc-all-on');
      allOn.setAttribute('data-bsc-op', OP.ALL_ON);
      allOn.textContent = 'Enable all';
      const allOff = element(doc, 'button', 'DialogButton', 'bsc-all-off');
      allOff.setAttribute('data-bsc-op', OP.ALL_OFF);
      allOff.textContent = 'Disable all';
      actions.appendChild(extra);
      actions.appendChild(allOn);
      actions.appendChild(allOff);
      panel.textContent = '';
      panel.appendChild(actions);
      panel.appendChild(note);
      panel.appendChild(list);
    }

    empty.style.display = view.empty ? '' : 'none';
    if (view.empty) {
      empty.children[0].textContent = view.empty.title || '';
      empty.children[1].textContent = view.empty.text || '';
    }

    const existing = {};
    Array.prototype.forEach.call(list.querySelectorAll('.bsc-row'), function (row) {
      existing[row.getAttribute('data-bsc-id')] = row;
    });
    let previous = empty;
    view.stations.forEach(function (station) {
      let row = existing[station.id];
      delete existing[station.id];
      if (!row) row = makeRow(doc);
      updateRow(row, station);
      if (previous.nextSibling !== row) previous.after(row);
      previous = row;
    });
    Object.keys(existing).forEach(function (id) {
      if (st.edit && st.edit.id === id) finishEdit(false);
      existing[id].remove();
    });

    if (note.textContent !== view.note) note.textContent = view.note;
    const noteClass = view.noteTone ? 'bsc-tone-' + view.noteTone : '';
    if (note.className !== noteClass) note.className = noteClass;
    note.style.paddingBottom = view.note ? '8px' : '0';

    const extra = doc.getElementById('bsc-extra');
    const allOn = doc.getElementById('bsc-all-on');
    const allOff = doc.getElementById('bsc-all-off');
    extra.hidden = !view.extra;
    if (view.extra) {
      extra.setAttribute('data-bsc-op', view.extra.op);
      extra.textContent = view.extra.label;
    }
    allOn.hidden = !view.showAll;
    allOff.hidden = !view.showAll;
    allOn.disabled = !view.allOn;
    allOff.disabled = !view.allOff;
    allOn.classList.toggle('Primary', view.allOn);
    actions.style.display = view.extra || view.showAll ? '' : 'none';
  }

  function renderOptions(doc) {
    const wrapper = doc.getElementById('bsc-wrapper');
    if (!wrapper) return;
    const items = viewData().options;
    let card = doc.getElementById('bsc-options');
    let backdrop = doc.getElementById('bsc-options-backdrop');
    if (!st.optionsOpen || !items || !st.tabActive) {
      if (card) card.remove();
      if (backdrop) backdrop.remove();
      st.drawnOptions = '';
      return;
    }
    if (!card) {
      backdrop = element(doc, 'div', '', 'bsc-options-backdrop');
      backdrop.addEventListener('click', onBackdropClick);
      card = element(doc, 'div', '', 'bsc-options');
      card.setAttribute('role', 'dialog');
      card.setAttribute('aria-label', 'Base Station options');
      card.addEventListener('click', onOptionsClick);
      const head = element(doc, 'div', '', 'bsc-options-head');
      const title = doc.createElement('span');
      title.textContent = 'Options';
      head.appendChild(title);
      head.appendChild(iconButton(doc, 'bsc-options-close', '', 'Close', CROSS));
      card.appendChild(head);
      card.appendChild(element(doc, 'div', '', 'bsc-options-list'));
      wrapper.appendChild(backdrop);
      wrapper.appendChild(card);
      st.drawnOptions = '';
    }
    const list = doc.getElementById('bsc-options-list');
    const layout = JSON.stringify(items.map(function (item) { return [item.key, item.section, item.title]; }));
    const rebuilt = layout !== st.drawnOptions;
    if (rebuilt) {
      st.drawnOptions = layout;
      list.textContent = '';
      let section = null;
      items.forEach(function (item) {
        if (item.section !== section) {
          section = item.section;
          const heading = element(doc, 'div', 'bsc-section');
          heading.textContent = section;
          list.appendChild(heading);
        }
        const row = element(doc, 'div', 'bsc-option');
        row.setAttribute('data-bsc-option', item.key);
        const text = element(doc, 'div', 'bsc-text');
        const title = element(doc, 'div', 'bsc-option-title');
        title.textContent = item.title;
        text.appendChild(title);
        text.appendChild(element(doc, 'div', 'bsc-option-detail'));
        row.appendChild(text);
        row.appendChild(makeToggle(doc));
        list.appendChild(row);
      });
    }
    items.forEach(function (item) {
      const row = Array.prototype.filter.call(list.children, function (child) {
        return child.getAttribute('data-bsc-option') === item.key;
      })[0];
      if (!row) return;
      const detail = row.querySelector('.bsc-option-detail');
      const detailClass = 'bsc-option-detail' + (item.tone ? ' bsc-tone-' + item.tone : '');
      if (detail.className !== detailClass) detail.className = detailClass;
      if (detail.textContent !== (item.detail || '')) detail.textContent = item.detail || '';
      setToggle(row.querySelector('.bsc-toggle'), item.on, false, item.section + ': ' + item.title);
    });
    placeOptions(wrapper, card, rebuilt);
  }

  function placeOptions(wrapper, card, rebuilt) {
    const margin = 6;
    const room = wrapper.clientHeight;
    if (!room) return;
    if (rebuilt) {
      if (card.classList.contains('bsc-compact')) card.classList.remove('bsc-compact');
      if (card.scrollHeight > room - 2 * margin) card.classList.add('bsc-compact');
    }
    const header = wrapper.firstElementChild;
    const below = (header ? header.offsetTop + header.offsetHeight : 56) + 2;
    const height = Math.min(card.scrollHeight, room - 2 * margin);
    const top = Math.max(margin, Math.min(below, room - margin - height)) + 'px';
    const maxHeight = room - 2 * margin + 'px';
    if (card.style.top !== top) card.style.top = top;
    if (card.style.maxHeight !== maxHeight) card.style.maxHeight = maxHeight;
  }

  function renderEverything() {
    const icon = document.getElementById('bsc-icon');
    if (icon) renderIcon(icon);
    if (st.popupDoc) syncPopup();
  }

  function onVisibilityChange() {
    notify('visibility');
  }

  function onTimer() {
    const stale = isStale();
    const pruned = prunePending();
    if (stale !== st.wasStale || pruned) {
      st.wasStale = stale;
      renderEverything();
    }
    const apps = appStates();
    if (apps && JSON.stringify(apps) !== st.reportedApps) notify('apps');
  }

  function appStates() {
    const ids = st.data && Array.isArray(st.data.watchApps) ? st.data.watchApps : [];
    if (!ids.length) return undefined;
    let store = null;
    let ui = null;
    try {
      store = window.opener && window.opener.appStore;
      ui = window.opener && window.opener.SteamUIStore;
    } catch (error) {
      return undefined;
    }
    if (!store || typeof store.GetAppOverviewByAppID !== 'function') return undefined;
    let streamed = [];
    let local = [];
    try { streamed = ui && Array.isArray(ui.m_remoteClientStreams) ? ui.m_remoteClientStreams.map(Number) : []; } catch (error) { }
    try { local = ui && ui.RunningApps ? Array.prototype.map.call(ui.RunningApps, function (app) { return Number(app && app.appid); }) : []; } catch (error) { }
    return ids.map(function (id) {
      let overview = null;
      try { overview = store.GetAppOverviewByAppID(id); } catch (error) { }
      const clients = overview && Array.isArray(overview.per_client_data) ? overview.per_client_data : [];
      const busy = clients.some(function (client) {
        return client && (client.display_status === APP_RUNNING || client.display_status === APP_LAUNCHING || client.streaming_to_local_client === true);
      });
      return {
        id: id,
        known: !!overview,
        installed: clients.some(function (client) { return client && client.installed === true; }),
        running: busy || streamed.indexOf(id) >= 0 || local.indexOf(id) >= 0
      };
    });
  }

  function report(state) {
    const out = {
      state: state,
      visible: document.visibilityState === 'visible',
      popupOpen: st.popupOpen,
      popup: st.popupDoc ? 'attached' : 'missing',
      actions: st.queue
    };
    const apps = appStates();
    if (apps) {
      out.apps = apps;
      st.reportedApps = JSON.stringify(apps);
    }
    st.queue = [];
    return JSON.stringify(out);
  }

  function clearHandled(map, handled) {
    map.forEach(function (entry, key) {
      if (entry.seq <= handled.seq) map.delete(key);
    });
  }

  function update(data) {
    if (data) {
      st.data = data;
      st.lastData = Date.now();
      const handled = data.handled || {};
      if (handled.session === st.session) {
        clearHandled(st.pending, handled);
        clearHandled(st.optionPending, handled);
        clearHandled(st.renamePending, handled);
      }
    }
    attachPopup();
    const state = ensureIcon();
    if (st.popupDoc) syncPopup();
    return report(state);
  }

  function dispose() {
    clearInterval(st.timer);
    document.removeEventListener('visibilitychange', onVisibilityChange);
    detachPopup();
    unhookKeyboard();
    ['bsc-icon', 'bsc-bar-style'].forEach(function (id) {
      const node = document.getElementById(id);
      if (node) node.remove();
    });
    if (window.__bsc === api) delete window.__bsc;
    return 'removed';
  }

  document.addEventListener('visibilitychange', onVisibilityChange);
  st.timer = setInterval(onTimer, 1000);

  const api = { version: version, update: update, dispose: dispose };
  window.__bsc = api;
  return api;
})
