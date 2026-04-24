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
var QUALITY_SETTINGS_KEY = 'apollo_quality_settings_v1';
var QUALITY_CACHE_KEY = 'apollo_quality_cache_v1';
var QUALITY_CACHE_MS = 6 * 60 * 60 * 1000;
var CLAY_SETTINGS_KEY = 'clay-settings';
var sLastQualityRequest = null;

function normalizeWhitespace(text) {
  return String(text || '').replace(/\s+/g, ' ').trim();
}

function readPayloadField(payload, name) {
  if (payload == null) {
    return undefined;
  }
  if (payload[name] !== undefined) {
    return payload[name];
  }
  var numeric = MESSAGE_KEY[name];
  if (numeric !== undefined && payload[numeric] !== undefined) {
    return payload[numeric];
  }
  return undefined;
}

function unwrapClayValue(value) {
  if (value && typeof value === 'object' && value.value !== undefined) {
    return value.value;
  }
  return value;
}

function normalizeBoolean(value) {
  value = unwrapClayValue(value);
  if (typeof value === 'boolean') {
    return value;
  }
  if (typeof value === 'number') {
    return value !== 0;
  }
  if (typeof value === 'string') {
    value = value.toLowerCase();
    return value === 'true' || value === '1' || value === 'on';
  }
  return false;
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

function loadClaySettings() {
  try {
    var raw = localStorage.getItem(CLAY_SETTINGS_KEY);
    if (raw) {
      var parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') {
        return parsed;
      }
    }
  } catch (e) {}

  return null;
}

function loadSavedLocations() {
  try {
    var raw = localStorage.getItem('apollo_locations');
    if (raw) {
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed) && parsed.length > 0) {
        return normalizeLocations(parsed);
      }
    }
  } catch (e) {}

  return null;
}

function loadLocationsFromClaySettings() {
  var claySettings = loadClaySettings();

  if (!claySettings || !claySettings.LOCATIONS_JSON) {
    return null;
  }

  try {
    var parsed = JSON.parse(claySettings.LOCATIONS_JSON);
    if (Array.isArray(parsed) && parsed.length > 0) {
      return normalizeLocations(parsed);
    }
  } catch (e) {}

  return null;
}

function loadLocations() {
  return loadSavedLocations() ||
    loadLocationsFromClaySettings() ||
    normalizeLocations(JSON.parse(clayConfig.DEFAULT_LOCATIONS_JSON));
}

function saveLocations(locations) {
  try {
    localStorage.setItem('apollo_locations', JSON.stringify(locations));
  } catch (e) {}
}

function loadSavedQualitySettings() {
  try {
    var raw = localStorage.getItem(QUALITY_SETTINGS_KEY);
    if (raw) {
      var parsed = JSON.parse(raw);
      return {
        enabled: !!parsed.enabled,
        apiKey: normalizeWhitespace(parsed.apiKey)
      };
    }
  } catch (e) {}

  return null;
}

function loadQualitySettingsFromClaySettings() {
  var claySettings = loadClaySettings();

  if (!claySettings || (
      claySettings.QUALITY_ENABLED === undefined &&
      claySettings.QUALITY_API_KEY === undefined)) {
    return null;
  }

  return {
    enabled: normalizeBoolean(claySettings.QUALITY_ENABLED),
    apiKey: normalizeWhitespace(claySettings.QUALITY_API_KEY)
  };
}

function loadQualitySettings() {
  return loadSavedQualitySettings() ||
    loadQualitySettingsFromClaySettings() || {
      enabled: false,
      apiKey: ''
    };
}

function saveQualitySettings(settings) {
  try {
    localStorage.setItem(QUALITY_SETTINGS_KEY, JSON.stringify({
      enabled: !!settings.enabled,
      apiKey: normalizeWhitespace(settings.apiKey)
    }));
  } catch (e) {}
}

function loadQualityCache() {
  try {
    var raw = localStorage.getItem(QUALITY_CACHE_KEY);
    if (raw) {
      var parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object' && parsed.entries) {
        return parsed;
      }
    }
  } catch (e) {}

  return { entries: {} };
}

function saveQualityCache(cache) {
  try {
    localStorage.setItem(QUALITY_CACHE_KEY, JSON.stringify(cache));
  } catch (e) {}
}

function clearQualityCache() {
  try {
    localStorage.removeItem(QUALITY_CACHE_KEY);
  } catch (e) {}
}

function locationsEqual(a, b) {
  return JSON.stringify(normalizeLocations(a || [])) === JSON.stringify(normalizeLocations(b || []));
}

function qualitySettingsEqual(a, b) {
  return !!a && !!b &&
    !!a.enabled === !!b.enabled &&
    normalizeWhitespace(a.apiKey) === normalizeWhitespace(b.apiKey);
}

function syncLocalStateFromClaySettings() {
  var clayLocations = loadLocationsFromClaySettings();
  var clayQualitySettings = loadQualitySettingsFromClaySettings();
  var savedLocations = loadSavedLocations();
  var savedQualitySettings = loadSavedQualitySettings();
  var locationsChanged = false;
  var qualityChanged = false;

  if (clayLocations && !locationsEqual(savedLocations, clayLocations)) {
    saveLocations(clayLocations);
    locationsChanged = true;
  }

  if (clayQualitySettings &&
      (!savedQualitySettings || !qualitySettingsEqual(savedQualitySettings, clayQualitySettings))) {
    saveQualitySettings(clayQualitySettings);
    qualityChanged = true;
  }

  if (locationsChanged || qualityChanged) {
    clearQualityCache();
  }
}

function setConfigDefaultByMessageKey(items, messageKey, value) {
  for (var i = 0; i < items.length; i++) {
    var item = items[i];
    if (item.messageKey === messageKey) {
      item.defaultValue = value;
      return true;
    }
    if (item.items && setConfigDefaultByMessageKey(item.items, messageKey, value)) {
      return true;
    }
  }
  return false;
}

function applyStoredConfigDefaults() {
  var qualitySettings = loadQualitySettings();

  setConfigDefaultByMessageKey(clayConfig, 'LOCATIONS_JSON', JSON.stringify(loadLocations()));
  setConfigDefaultByMessageKey(clayConfig, 'QUALITY_ENABLED', qualitySettings.enabled);
  setConfigDefaultByMessageKey(clayConfig, 'QUALITY_API_KEY', qualitySettings.apiKey);
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

function buildQualityCacheKey(locIndex, dateInt, type) {
  return [locIndex, dateInt, type].join(':');
}

function getFreshQualityCacheEntry(cache, locIndex, dateInt, type) {
  var entry = cache.entries[buildQualityCacheKey(locIndex, dateInt, type)];
  if (!entry) {
    return null;
  }
  if (Date.now() - entry.updatedAt > QUALITY_CACHE_MS) {
    return null;
  }
  return entry;
}

function setQualityCacheEntry(cache, locIndex, dateInt, type, text) {
  cache.entries[buildQualityCacheKey(locIndex, dateInt, type)] = {
    updatedAt: Date.now(),
    text: text || ''
  };
}

function dateIntToApiDate(dateInt) {
  var numeric = parseInt(dateInt, 10) || 0;
  var year = Math.floor(numeric / 10000);
  var month = Math.floor((numeric % 10000) / 100);
  var day = numeric % 100;

  function pad2(value) {
    return value < 10 ? '0' + value : '' + value;
  }

  if (!year || !month || !day) {
    return '';
  }

  return year + '-' + pad2(month) + '-' + pad2(day);
}

function localDateIntForOffset(offsetMinutes, addDays) {
  var date = new Date(Date.now() + offsetMinutes * 60000 + (addDays || 0) * 86400000);
  return date.getUTCFullYear() * 10000 +
    (date.getUTCMonth() + 1) * 100 +
    date.getUTCDate();
}

function normalizeQualityText(text) {
  return normalizeWhitespace(text).substring(0, 15);
}

function sendQualityPayload(locIndex, date0, date1, texts, doneFn) {
  var message = {};
  message[MESSAGE_KEY.QUALITY_LOC_INDEX] = locIndex;
  message[MESSAGE_KEY.QUALITY_DATE_0] = date0;
  message[MESSAGE_KEY.QUALITY_DATE_1] = date1;
  message[MESSAGE_KEY.QUALITY_SUNRISE_0] = texts.sunrise0 || '';
  message[MESSAGE_KEY.QUALITY_SUNSET_0] = texts.sunset0 || '';
  message[MESSAGE_KEY.QUALITY_SUNRISE_1] = texts.sunrise1 || '';
  message[MESSAGE_KEY.QUALITY_SUNSET_1] = texts.sunset1 || '';

  Pebble.sendAppMessage(message,
    function() {
      if (doneFn) {
        doneFn();
      }
    },
    function(error) {
      console.log('Quality payload send failed: ' + error.error);
    }
  );
}

function buildImmediateQualityPayload(locations) {
  var payload = {};
  var locIndex = 0;
  var location;
  var effectiveOffset = 0;

  if (!locations.length) {
    return null;
  }

  if (sLastQualityRequest) {
    locIndex = parseInt(sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_LOC_INDEX], 10);
    if (isNaN(locIndex) || locIndex < 0 || locIndex >= locations.length) {
      locIndex = 0;
    }
  }

  location = locations[locIndex];
  effectiveOffset = (location.baseOffset || 0) + (location.dst ? 60 : 0);

  payload[MESSAGE_KEY.QUALITY_REQ_LOC_INDEX] = locIndex;
  payload[MESSAGE_KEY.QUALITY_REQ_DATE_0] = sLastQualityRequest ?
    (parseInt(sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_DATE_0], 10) || localDateIntForOffset(effectiveOffset, 0)) :
    localDateIntForOffset(effectiveOffset, 0);
  payload[MESSAGE_KEY.QUALITY_REQ_DATE_1] = sLastQualityRequest ?
    (parseInt(sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_DATE_1], 10) || localDateIntForOffset(effectiveOffset, 1)) :
    localDateIntForOffset(effectiveOffset, 1);

  return payload;
}

function fetchQualityEvent(apiKey, location, dateInt, type, doneFn) {
  var apiDate = dateIntToApiDate(dateInt);
  var xhr = new XMLHttpRequest();

  if (!apiDate) {
    doneFn('');
    return;
  }

  xhr.open(
    'GET',
    'https://api.sunsethue.com/event?latitude=' +
      encodeURIComponent(location.lat.toFixed(6)) +
      '&longitude=' +
      encodeURIComponent(location.lon.toFixed(6)) +
      '&date=' +
      encodeURIComponent(apiDate) +
      '&type=' +
      encodeURIComponent(type)
  );
  xhr.setRequestHeader('x-api-key', apiKey);
  xhr.timeout = 15000;
  xhr.onload = function() {
    var payload;
    if (xhr.status !== 200) {
      console.log('Sunsethue request failed (' + type + ' ' + apiDate + '): ' + xhr.status);
      doneFn('');
      return;
    }

    try {
      payload = JSON.parse(xhr.responseText);
    } catch (e) {
      console.log('Sunsethue response parse failed: ' + e);
      doneFn('');
      return;
    }

    doneFn(normalizeQualityText((((payload || {}).data || {}).quality_text) || ''));
  };
  xhr.onerror = function() {
    console.log('Sunsethue network error (' + type + ' ' + apiDate + ')');
    doneFn('');
  };
  xhr.ontimeout = function() {
    console.log('Sunsethue timeout (' + type + ' ' + apiDate + ')');
    doneFn('');
  };
  xhr.send();
}

function syncQualityForPayload(payload) {
  var settings = loadQualitySettings();
  var locations = loadLocations();
  var locIndex = parseInt(readPayloadField(payload, 'QUALITY_REQ_LOC_INDEX'), 10);
  var location;
  var effectiveOffset;
  var date0 = parseInt(readPayloadField(payload, 'QUALITY_REQ_DATE_0'), 10) || 0;
  var date1 = parseInt(readPayloadField(payload, 'QUALITY_REQ_DATE_1'), 10) || 0;
  var forceRefresh = normalizeBoolean(readPayloadField(payload, 'QUALITY_FORCE_REFRESH'));
  var cache = loadQualityCache();
  var texts = {
    sunrise0: '',
    sunset0: '',
    sunrise1: '',
    sunset1: ''
  };
  var requests = [
    { key: 'sunrise0', type: 'sunrise', date: date0 },
    { key: 'sunset0', type: 'sunset', date: date0 },
    { key: 'sunrise1', type: 'sunrise', date: date1 },
    { key: 'sunset1', type: 'sunset', date: date1 }
  ];
  var pending = 0;

  if (isNaN(locIndex) || locIndex < 0 || locIndex >= locations.length) {
    sendQualityPayload(0, date0, date1, texts);
    return;
  }

  location = locations[locIndex];
  effectiveOffset = (location.baseOffset || 0) + (location.dst ? 60 : 0);
  if (!date0) {
    date0 = localDateIntForOffset(effectiveOffset, 0);
    requests[0].date = date0;
    requests[1].date = date0;
  }
  if (!date1) {
    date1 = localDateIntForOffset(effectiveOffset, 1);
    requests[2].date = date1;
    requests[3].date = date1;
  }

  if (!settings.enabled || !settings.apiKey) {
    sendQualityPayload(locIndex, date0, date1, texts);
    return;
  }

  requests.forEach(function(request) {
    var cached = forceRefresh ? null :
      getFreshQualityCacheEntry(cache, locIndex, request.date, request.type);
    if (cached) {
      texts[request.key] = cached.text || '';
      return;
    }

    pending += 1;
    fetchQualityEvent(settings.apiKey, location, request.date, request.type, function(text) {
      texts[request.key] = text || '';
      setQualityCacheEntry(cache, locIndex, request.date, request.type, texts[request.key]);
      pending -= 1;
      if (pending === 0) {
        saveQualityCache(cache);
        sendQualityPayload(locIndex, date0, date1, texts);
      }
    });
  });

  if (pending === 0) {
    sendQualityPayload(locIndex, date0, date1, texts);
  }
}

Pebble.addEventListener('ready', function() {
  console.log('Apollo JS ready');
  syncLocalStateFromClaySettings();
  applyStoredConfigDefaults();
  sendLocations(loadLocations());
});

Pebble.addEventListener('showConfiguration', function() {
  applyStoredConfigDefaults();
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event.response || event.response === 'CANCELLED') {
    return;
  }

  try {
    var previousLocations = JSON.stringify(loadLocations());
    var previousQualitySettings = loadQualitySettings();
    var settings = clay.getSettings(event.response, false);
    var rawLocations = unwrapClayValue(settings.LOCATIONS_JSON);
    var locations = normalizeLocations(JSON.parse(rawLocations || '[]'));
    var qualitySettings = {
      enabled: normalizeBoolean(settings.QUALITY_ENABLED),
      apiKey: normalizeWhitespace(unwrapClayValue(settings.QUALITY_API_KEY))
    };
    var nextLocations = JSON.stringify(locations);
    var qualityChanged = previousQualitySettings.enabled !== qualitySettings.enabled ||
      previousQualitySettings.apiKey !== qualitySettings.apiKey;

    if (locations.length > 0) {
      saveLocations(locations);
    }
    saveQualitySettings(qualitySettings);

    if (previousLocations !== nextLocations || qualityChanged) {
      clearQualityCache();
    }

    applyStoredConfigDefaults();

    if (locations.length > 0) {
      var immediateQualityPayload = qualitySettings.enabled ?
        buildImmediateQualityPayload(locations) :
        null;

      sendLocations(locations, function() {
        console.log('Synced ' + locations.length + ' locations');
        if (immediateQualityPayload) {
          syncQualityForPayload(immediateQualityPayload);
        }
      });
    }
  } catch (error) {
    console.log('webviewclosed error: ' + error);
  }
});

Pebble.addEventListener('appmessage', function(event) {
  var payload = event.payload || {};

  syncLocalStateFromClaySettings();

  if (readPayloadField(payload, 'SYNC_REQUEST')) {
    console.log('Received sync request from watch');
    sendLocations(loadLocations(), function() {
      console.log('Synced locations after watch request');
    });
  }

  if (readPayloadField(payload, 'QUALITY_REQUEST')) {
    sLastQualityRequest = {};
    sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_LOC_INDEX] =
      readPayloadField(payload, 'QUALITY_REQ_LOC_INDEX');
    sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_DATE_0] =
      readPayloadField(payload, 'QUALITY_REQ_DATE_0');
    sLastQualityRequest[MESSAGE_KEY.QUALITY_REQ_DATE_1] =
      readPayloadField(payload, 'QUALITY_REQ_DATE_1');
    syncQualityForPayload(payload);
  }
});
