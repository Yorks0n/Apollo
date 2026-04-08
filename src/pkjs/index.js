/* Apollo — PebbleKit JS
 *
 * Uses Clay (@rebble/clay) to render a declarative configuration page and
 * enhances it with search, geolocation, timezone lookup, and validation.
 *
 * Location model: { name, lat, lon, baseOffset (min), dst (bool) }
 * Effective UTC offset sent to watch = baseOffset + (dst ? 60 : 0)
 */

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customFn = require('./customFn');
var MESSAGE_KEY = require('message_keys');

var clay = new Clay(clayConfig, customFn, { autoHandleEvents: false });

var MAX_LOCATIONS = clayConfig.MAX_LOCATIONS || 8;

function normalizeWhitespace(text) {
  return String(text || '').replace(/\s+/g, ' ').trim();
}

function toEnglishName(text, fallback) {
  var normalized = normalizeWhitespace(text);
  if (!normalized) {
    return fallback || '';
  }

  if (!/^[\x20-\x7E]+$/.test(normalized)) {
    normalized = normalizeWhitespace(normalized.replace(/[^\x20-\x7E]/g, ''));
  }

  if (!normalized) {
    return fallback || '';
  }

  return normalized.substring(0, 23);
}

function normalizeLocation(loc) {
  return {
    name: toEnglishName(loc.name, 'Location'),
    lat: typeof loc.lat === 'number' ? loc.lat : parseFloat(loc.lat || 0),
    lon: typeof loc.lon === 'number' ? loc.lon : parseFloat(loc.lon || 0),
    baseOffset: loc.baseOffset !== undefined ?
      (parseInt(loc.baseOffset, 10) || 0) :
      (parseInt(loc.utcOffset, 10) || 0),
    dst: !!loc.dst
  };
}

function normalizeLocations(rawLocations) {
  if (!Array.isArray(rawLocations)) {
    return [];
  }

  return rawLocations.slice(0, MAX_LOCATIONS).map(normalizeLocation);
}

function loadLocations() {
  try {
    var raw = localStorage.getItem('apollo_locations');
    if (raw) {
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed) && parsed.length > 0) {
        return normalizeLocations(parsed);
      }
    }
  } catch (e) {}

  return normalizeLocations(JSON.parse(clayConfig.DEFAULT_LOCATIONS_JSON));
}

function saveLocations(locations) {
  try {
    localStorage.setItem('apollo_locations', JSON.stringify(locations));
  } catch (e) {}
}

function sendLocations(locations, doneFn) {
  var locs = normalizeLocations(locations);

  function sendOne(index) {
    if (index >= locs.length) {
      var doneMessage = {};
      doneMessage[MESSAGE_KEY.LOC_SYNC_DONE] = 1;
      Pebble.sendAppMessage(doneMessage,
        function() {
          if (doneFn) {
            doneFn();
          }
        },
        function(error) {
          console.log('LOC_SYNC_DONE failed: ' + error.error);
        }
      );
      return;
    }

    var loc = locs[index];
    var message = {};
    message[MESSAGE_KEY.LOC_INDEX] = index;
    message[MESSAGE_KEY.LOC_NAME] = loc.name.substring(0, 23) || 'Location';
    message[MESSAGE_KEY.LOC_LAT] = Math.round(loc.lat * 1000000);
    message[MESSAGE_KEY.LOC_LON] = Math.round(loc.lon * 1000000);
    message[MESSAGE_KEY.LOC_UTC_OFFSET] = (loc.baseOffset || 0) + (loc.dst ? 60 : 0);

    Pebble.sendAppMessage(message,
      function() {
        sendOne(index + 1);
      },
      function(error) {
        console.log('Location ' + index + ' send failed, retrying: ' + error.error);
        Pebble.sendAppMessage(message,
          function() {
            sendOne(index + 1);
          },
          function() {
            sendOne(index + 1);
          }
        );
      }
    );
  }

  var countMessage = {};
  countMessage[MESSAGE_KEY.LOC_COUNT] = locs.length;
  Pebble.sendAppMessage(countMessage,
    function() {
      sendOne(0);
    },
    function(error) {
      console.log('LOC_COUNT failed: ' + error.error);
    }
  );
}

Pebble.addEventListener('ready', function() {
  console.log('Apollo JS ready');
  sendLocations(loadLocations());
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event.response || event.response === 'CANCELLED') {
    return;
  }

  try {
    var settings = clay.getSettings(event.response, false);
    var rawLocations = settings.LOCATIONS_JSON;
    if (rawLocations && typeof rawLocations === 'object' &&
        rawLocations.value !== undefined) {
      rawLocations = rawLocations.value;
    }

    var locations = normalizeLocations(JSON.parse(rawLocations || '[]'));
    if (locations.length > 0) {
      saveLocations(locations);
      sendLocations(locations, function() {
        console.log('Synced ' + locations.length + ' locations');
      });
    }
  } catch (error) {
    console.log('webviewclosed error: ' + error);
  }
});

Pebble.addEventListener('appmessage', function(event) {
  if (!event.payload || !event.payload[MESSAGE_KEY.SYNC_REQUEST]) {
    return;
  }

  console.log('Received sync request from watch');
  sendLocations(loadLocations(), function() {
    console.log('Synced locations after watch request');
  });
});
