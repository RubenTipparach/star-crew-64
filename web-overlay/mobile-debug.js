// star-crew-64 — mobile-friendly debug overlay
//
// The emulator's index.html already has a #output log panel, but on phones
// it's only 80px tall and gets hidden once `enterGameMode()` adds the
// `game-fullscreen` class. This script:
//   1. Pins #output as a full-width fixed bottom strip
//   2. Keeps it visible even in game-fullscreen mode
//   3. Hides the emulator's stray Copy buttons (input-panel, profile-panel)
//      that are dev-only and crowd the overlay
//   4. Silences profile-server heartbeat + build-info log spam (no such
//      server exists on static Pages hosting)
//   5. Mirrors console.error / console.warn / window error events / unhandled
//      rejections into the log (the emulator's own log() doesn't capture
//      those)
//   6. Adds a Hide/Show toggle so it can be dismissed once the game runs
(function() {
  var styles = document.createElement('style');
  styles.textContent = [
    '#output {',
    '  position: fixed !important;',
    '  left: 0 !important;',
    '  right: 0 !important;',
    '  bottom: 0 !important;',
    '  width: 100% !important;',
    '  max-width: none !important;',
    '  max-height: 45vh !important;',
    '  margin: 0 !important;',
    '  border-radius: 0 !important;',
    '  border-top: 2px solid #0f3460 !important;',
    '  z-index: 999999 !important;',
    '  font-size: 11px !important;',
    '  padding-bottom: 28px !important;',
    '}',
    // Force the #output and its (hidden-on-mobile) wrapper to show.
    '.desktop-only { display: block !important; }',
    'body.game-fullscreen #output { display: block !important; }',
    // Hide the emulator's Copy/dev buttons + floating panels — no profile
    // server on static hosting and our overlay has its own Hide button.
    '#btn-copy, #btn-copy-input, #btn-copy-profile,',
    '#input-panel, #profile-panel { display: none !important; }',
    '#mobile-log-toggle {',
    '  position: fixed; right: 6px; bottom: 6px;',
    '  z-index: 1000000;',
    '  background: #16213e; color: #e0e0e0;',
    '  border: 1px solid #0f3460; border-radius: 4px;',
    '  padding: 4px 10px; font-size: 11px;',
    '  font-family: system-ui, sans-serif;',
    '  cursor: pointer;',
    '}'
  ].join('\n');
  document.head.appendChild(styles);

  // ── Silence profile-server / build-info noise ─────────────────────────
  // sendProfileToServer logs "[heartbeat] ws not connected, sample dropped"
  // every few seconds when the profile WebSocket isn't reachable — that
  // server only exists during local dev (profile-server.js). No-op it here.
  if (typeof window.sendProfileToServer === 'function') {
    window.sendProfileToServer = function() {};
  }
  // Filter any other heartbeat lines still logged via the global log()
  // (e.g. connection-lost messages from the WS reconnect loop).
  if (typeof window.log === 'function') {
    var origLog = window.log;
    window.log = function(msg) {
      if (typeof msg === 'string' &&
          (msg.indexOf('[heartbeat]') === 0 ||
           msg.indexOf('[ws]') === 0 ||
           msg.indexOf('[build-info]') === 0)) {
        return;
      }
      return origLog.call(this, msg);
    };
  }

  function init() {
    var output = document.getElementById('output');
    if (!output) return;

    var btn = document.createElement('button');
    btn.id = 'mobile-log-toggle';
    btn.textContent = 'Hide log';
    document.body.appendChild(btn);
    btn.addEventListener('click', function() {
      var hidden = output.style.display === 'none';
      output.style.display = hidden ? '' : 'none';
      btn.textContent = hidden ? 'Hide log' : 'Show log';
    });

    function append(line) {
      output.textContent += '[' + new Date().toLocaleTimeString() + '] ' + line + '\n';
      output.scrollTop = output.scrollHeight;
    }

    var origErr = console.error, origWarn = console.warn;
    console.error = function() {
      try { append('[err] ' + Array.prototype.join.call(arguments, ' ')); } catch (e) {}
      origErr.apply(console, arguments);
    };
    console.warn = function() {
      try { append('[warn] ' + Array.prototype.join.call(arguments, ' ')); } catch (e) {}
      origWarn.apply(console, arguments);
    };
    window.addEventListener('error', function(e) {
      append('[js-err] ' + (e.message || '?') + ' @ ' + (e.filename || '?') + ':' + (e.lineno || '?'));
    });
    window.addEventListener('unhandledrejection', function(e) {
      var msg = e.reason && e.reason.message ? e.reason.message : String(e.reason);
      append('[reject] ' + msg);
    });
  }
  if (document.body) init(); else document.addEventListener('DOMContentLoaded', init);
})();
