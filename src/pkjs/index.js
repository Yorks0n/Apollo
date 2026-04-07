/* Apollo — PebbleKit JS
 *
 * Responsibilities:
 *  1. On 'ready': send default/stored locations to the watch.
 *  2. On 'showConfiguration': open the config page.
 *  3. On 'webviewclosed': parse saved locations and sync to the watch.
 */

var MESSAGE_KEY = {
  LOC_COUNT:      0,
  LOC_INDEX:      1,
  LOC_NAME:       2,
  LOC_LAT:        3,
  LOC_LON:        4,
  LOC_UTC_OFFSET: 5,
  LOC_SYNC_DONE:  6
};

// ---------------------------------------------------------------------------
// Default locations (used when no config has been saved yet)
// ---------------------------------------------------------------------------
var DEFAULT_LOCATIONS = [
  { name: 'London',   lat:  51.5074,  lon:  -0.1278, utcOffset:    0 },
  { name: 'New York', lat:  40.7128,  lon: -74.0060, utcOffset: -300 },
  { name: 'Tokyo',    lat:  35.6762,  lon: 139.6503, utcOffset:  540 },
  { name: 'Sydney',   lat: -33.8688,  lon: 151.2093, utcOffset:  600 }
];

// ---------------------------------------------------------------------------
// Storage helpers
// ---------------------------------------------------------------------------
function loadLocations() {
  try {
    var raw = localStorage.getItem('apollo_locations');
    if (raw) {
      var locs = JSON.parse(raw);
      if (Array.isArray(locs) && locs.length > 0) return locs;
    }
  } catch (e) { /* ignore */ }
  return DEFAULT_LOCATIONS;
}

function saveLocations(locs) {
  try {
    localStorage.setItem('apollo_locations', JSON.stringify(locs));
  } catch (e) { /* ignore */ }
}

// ---------------------------------------------------------------------------
// Send locations to the watch
// ---------------------------------------------------------------------------
function sendLocations(locations, successFn, failFn) {
  var locs = locations.slice(0, 12); // max 12

  function sendOne(index) {
    if (index >= locs.length) {
      // All sent: send LOC_SYNC_DONE
      Pebble.sendAppMessage(
        { [MESSAGE_KEY.LOC_SYNC_DONE]: 1 },
        function() { if (successFn) successFn(); },
        function(e) { console.log('SYNC_DONE failed: ' + e.error); if (failFn) failFn(e); }
      );
      return;
    }

    var loc = locs[index];
    var msg = {};
    msg[MESSAGE_KEY.LOC_INDEX]      = index;
    msg[MESSAGE_KEY.LOC_NAME]       = (loc.name || 'Location').substring(0, 23);
    msg[MESSAGE_KEY.LOC_LAT]        = Math.round((loc.lat  || 0) * 1000000);
    msg[MESSAGE_KEY.LOC_LON]        = Math.round((loc.lon  || 0) * 1000000);
    msg[MESSAGE_KEY.LOC_UTC_OFFSET] = Math.round(loc.utcOffset || 0);

    Pebble.sendAppMessage(msg,
      function() { sendOne(index + 1); },
      function(e) {
        console.log('Failed to send location ' + index + ': ' + e.error);
        // Retry once
        Pebble.sendAppMessage(msg,
          function() { sendOne(index + 1); },
          function(e2) {
            console.log('Retry also failed: ' + e2.error);
            sendOne(index + 1); // skip and continue
          }
        );
      }
    );
  }

  // Send count first
  var countMsg = {};
  countMsg[MESSAGE_KEY.LOC_COUNT] = locs.length;
  Pebble.sendAppMessage(countMsg,
    function() { sendOne(0); },
    function(e) { console.log('LOC_COUNT failed: ' + e.error); if (failFn) failFn(e); }
  );
}

// ---------------------------------------------------------------------------
// Event listeners
// ---------------------------------------------------------------------------
Pebble.addEventListener('ready', function() {
  console.log('Apollo JS ready');
  var locs = loadLocations();
  sendLocations(locs);
});

Pebble.addEventListener('showConfiguration', function() {
  var locs = loadLocations();
  // Encode current locations into the URL so the config page can pre-populate
  var encoded = encodeURIComponent(JSON.stringify(locs));
  var url = 'https://yorkson.github.io/apollo-config/?data=' + encoded;
  console.log('Opening config: ' + url);
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') return;

  try {
    var locs = JSON.parse(decodeURIComponent(e.response));
    if (Array.isArray(locs) && locs.length > 0) {
      saveLocations(locs);
      sendLocations(locs, function() {
        console.log('Locations synced after config');
      });
    }
  } catch (err) {
    console.log('webviewclosed parse error: ' + err);
  }
});
