/* jshint ignore:start */
module.exports = function(minified) {
  var clay = this;
  var MAX = 8;
  var hiddenLocationsItem;
  var editSlotItem;
  var searchQueryItem;
  var searchStatusItem;
  var searchResultsItem;
  var saveStatusItem;
  var searchButtonItem;
  var currentLocationButtonItem;
  var saveButtonItem;
  var rootFormEl;
  var searchSectionEl;
  var slots = [];

  function injectStyles() {
    if (document.getElementById('apollo-clay-style')) {
      return;
    }
    var styleEl = document.createElement('style');
    styleEl.id = 'apollo-clay-style';
    styleEl.textContent =
      '.apollo-status{display:block;padding:6px 0 2px;font-size:13px;line-height:1.4}' +
      '.apollo-status--info{color:#666}' +
      '.apollo-status--success{color:#1f7a1f}' +
      '.apollo-status--error{color:#b3261e}' +
      '.apollo-summary{display:block;padding:4px 0 0;font-size:13px;line-height:1.5;color:#555}' +
      '.apollo-summary strong{font-size:14px;color:#222}' +
      '.apollo-summary--muted{color:#777}' +
      '.apollo-results{display:flex;flex-direction:column;gap:8px;padding-top:6px}' +
      '.apollo-result{display:block;width:100%;padding:10px 12px;border:1px solid #d7d7d7;' +
        'border-radius:10px;background:#fff;text-align:left;font:inherit;color:#111}' +
      '.apollo-result strong{display:block;margin-bottom:4px}' +
      '.apollo-result span{display:block;font-size:13px;line-height:1.4;color:#555}' +
      '.apollo-result:active{background:#f4f4f4}' +
      '.apollo-hidden .description{display:none}';
    document.head.appendChild(styleEl);
  }

  function pad2(num) {
    return num < 10 ? '0' + num : '' + num;
  }

  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function roundNumber(value, precision) {
    var factor = Math.pow(10, precision);
    return Math.round(value * factor) / factor;
  }

  function normalizeWhitespace(text) {
    return String(text || '').replace(/\s+/g, ' ').trim();
  }

  function isAsciiText(text) {
    return /^[\x20-\x7E]+$/.test(text);
  }

  function toEnglishText(text) {
    var normalized = normalizeWhitespace(text);
    if (!normalized) {
      return '';
    }
    if (isAsciiText(normalized)) {
      return normalized;
    }
    return normalizeWhitespace(normalized.replace(/[^\x20-\x7E]/g, ''));
  }

  function fallbackLocationName(index) {
    return 'Location ' + (index + 1);
  }

  function toEnglishName(text, fallback) {
    var normalized = toEnglishText(text);
    if (normalized) {
      return normalized.substring(0, 23);
    }
    return fallback || '';
  }

  function getEnglishResultName(result, slotIndex) {
    var namedetails = result.namedetails || {};
    var candidates = [
      namedetails['name:en'],
      namedetails.name,
      result.name,
      String(result.display_name || '').split(',')[0]
    ];

    for (var i = 0; i < candidates.length; i++) {
      var candidate = toEnglishName(candidates[i], '');
      if (candidate) {
        return candidate;
      }
    }

    return fallbackLocationName(slotIndex);
  }

  function getEnglishResultDisplay(result, fallbackName) {
    var namedetails = result.namedetails || {};
    var candidates = [
      result.display_name,
      namedetails['display_name:en'],
      namedetails['name:en'],
      namedetails.name,
      result.name,
      fallbackName
    ];

    for (var i = 0; i < candidates.length; i++) {
      var candidate = toEnglishText(candidates[i]);
      if (candidate) {
        return candidate;
      }
    }

    return fallbackName;
  }

  function formatOffset(baseOffset, dst) {
    var total = baseOffset + (dst ? 60 : 0);
    var abs = Math.abs(total);
    return 'UTC' +
      (total < 0 ? '-' : '+') +
      pad2(Math.floor(abs / 60)) +
      ':' +
      pad2(abs % 60) +
      (dst ? ' · DST' : '');
  }

  function setStatus(item, type, message) {
    if (!message) {
      item.set('');
      return;
    }
    item.set(
      '<span class="apollo-status apollo-status--' + type + '">' +
      escapeHtml(message) +
      '</span>'
    );
  }

  function setSummary(slot, html) {
    slot.summary.set(html || '');
  }

  function clearMessages() {
    setStatus(saveStatusItem, 'info', '');
  }

  function normalizeLocation(loc) {
    return {
      name: toEnglishName(loc.name, ''),
      lat: typeof loc.lat === 'number' ? loc.lat : parseFloat(loc.lat || 0),
      lon: typeof loc.lon === 'number' ? loc.lon : parseFloat(loc.lon || 0),
      baseOffset: loc.baseOffset !== undefined ?
        parseInt(loc.baseOffset, 10) || 0 :
        (parseInt(loc.utcOffset, 10) || 0),
      dst: !!loc.dst
    };
  }

  function readStoredLocations() {
    var raw = hiddenLocationsItem ? hiddenLocationsItem.get() : '[]';
    try {
      var parsed = JSON.parse(raw || '[]');
      if (Array.isArray(parsed)) {
        return parsed.slice(0, MAX).map(normalizeLocation);
      }
    } catch (e) {}
    return [];
  }

  function getSlot(index) {
    var safeIndex = parseInt(index, 10);
    if (isNaN(safeIndex) || safeIndex < 0 || safeIndex >= slots.length) {
      safeIndex = 0;
    }
    return slots[safeIndex];
  }

  function getSectionElement(item) {
    if (!item || !item.$element || !item.$element[0]) {
      return null;
    }
    return item.$element[0].parentNode;
  }

  function setTargetSlot(index) {
    editSlotItem.set(String(getSlot(index).index));
  }

  function findFirstDisabledSlot() {
    for (var i = 0; i < slots.length; i++) {
      if (!slots[i].enabled.get()) {
        return i;
      }
    }
    return -1;
  }

  function setSectionVisible(sectionEl, visible) {
    if (!sectionEl) {
      return;
    }
    sectionEl.style.display = visible ? '' : 'none';
  }

  function isSlotBlank(slot) {
    return !normalizeWhitespace(slot.name.get()) &&
      !normalizeWhitespace(slot.lat.get()) &&
      !normalizeWhitespace(slot.lon.get());
  }

  function hasValidCoordinates(slot) {
    var lat = parseFloat(slot.lat.get());
    var lon = parseFloat(slot.lon.get());
    return !isNaN(lat) && lat >= -90 && lat <= 90 &&
      !isNaN(lon) && lon >= -180 && lon <= 180;
  }

  function updateSlotHeading(slot) {
    if (slot.isAddSlot) {
      slot.heading.set('Add location');
      return;
    }

    if (slot.enabled.get()) {
      slot.heading.set(toEnglishName(slot.name.get(), slot.title));
      return;
    }

    slot.heading.set(slot.title);
  }

  function toggleSearchControls(disabled) {
    if (disabled) {
      searchQueryItem.disable();
      searchButtonItem.disable();
      currentLocationButtonItem.disable();
      return;
    }

    searchQueryItem.enable();
    searchButtonItem.enable();
    currentLocationButtonItem.enable();
  }

  function toggleSlotFields(slot, enabled) {
    var showAddFields = slot.isAddSlot;
    var showExistingFields = enabled && !slot.isAddSlot;

    if (showAddFields || showExistingFields) {
      slot.name.show();
      slot.dst.show();
    } else {
      slot.name.hide();
      slot.dst.hide();
    }

    if (showAddFields) {
      slot.lat.show();
      slot.lon.show();
      slot.tz.show();
      slot.lookupButton.show();
    } else {
      slot.lat.hide();
      slot.lon.hide();
      slot.tz.hide();
      slot.lookupButton.hide();
    }

    slot.enabled.hide();

    if ((showAddFields || showExistingFields) && (enabled || !isSlotBlank(slot))) {
      slot.clearButton.show();
    } else {
      slot.clearButton.hide();
    }
  }

  function updateSlotSummary(slot) {
    var enabled = !!slot.enabled.get();
    if (slot.isAddSlot && !enabled) {
      if (isSlotBlank(slot)) {
        setSummary(
          slot,
          '<span class="apollo-summary apollo-summary--muted">Search below or enter an English name and coordinates here to add a new location.</span>'
        );
      } else {
        setSummary(
          slot,
          '<span class="apollo-summary apollo-summary--muted">This location will be added when you save.</span>'
        );
      }
      return;
    }

    if (!enabled) {
      setSummary(
        slot,
        '<span class="apollo-summary apollo-summary--muted">Disabled. Use search or manual entry, then enable this slot.</span>'
      );
      return;
    }

    var name = String(slot.name.get() || '').trim();
    var lat = parseFloat(slot.lat.get());
    var lon = parseFloat(slot.lon.get());
    var baseOffset = parseInt(slot.tz.get(), 10);

    if (!name || isNaN(lat) || isNaN(lon) || isNaN(baseOffset)) {
      setSummary(
        slot,
        '<span class="apollo-summary apollo-summary--muted">Enabled. Waiting for name, coordinates, and timezone.</span>'
      );
      return;
    }

    setSummary(
      slot,
      '<span class="apollo-summary">' +
        '<strong>' + escapeHtml(name) + '</strong><br>' +
        escapeHtml(lat.toFixed(4) + ', ' + lon.toFixed(4)) +
        ' · ' +
        escapeHtml(formatOffset(baseOffset, !!slot.dst.get())) +
      '</span>'
    );
  }

  function refreshSlot(slot) {
    updateSlotHeading(slot);
    toggleSlotFields(slot, !!slot.enabled.get());
    updateSlotSummary(slot);
  }

  function clearSlot(slot) {
    slot.enabled.set(false);
    slot.name.set('');
    slot.lat.set('');
    slot.lon.set('');
    slot.tz.set('0');
    slot.dst.set(false);
    refreshSlot(slot);
  }

  function fillSlot(slot, loc) {
    slot.enabled.set(true);
    slot.name.set(toEnglishName(loc.name, fallbackLocationName(slot.index)));
    slot.lat.set(String(roundNumber(loc.lat, 6)));
    slot.lon.set(String(roundNumber(loc.lon, 6)));
    slot.tz.set(String(loc.baseOffset || 0));
    slot.dst.set(!!loc.dst);
    refreshSlot(slot);
  }

  function hydrateFromLocations(locations) {
    for (var i = 0; i < slots.length; i++) {
      if (i < locations.length) {
        fillSlot(slots[i], locations[i]);
      } else {
        clearSlot(slots[i]);
      }
    }
    updateLayout();
  }

  function updateLayout() {
    var firstDisabled = findFirstDisabledSlot();
    var anchor = hiddenLocationsItem.$element[0];
    var maxMessage = 'Maximum of 8 locations reached. Clear one to add another.';

    for (var i = 0; i < slots.length; i++) {
      slots[i].isAddSlot = (i === firstDisabled);
      refreshSlot(slots[i]);
      setSectionVisible(slots[i].section, !!slots[i].enabled.get() || slots[i].isAddSlot);
    }

    for (var j = 0; j < slots.length; j++) {
      if (slots[j].enabled.get()) {
        rootFormEl.insertBefore(slots[j].section, anchor);
      }
    }

    rootFormEl.insertBefore(searchSectionEl, anchor);

    if (firstDisabled !== -1) {
      rootFormEl.insertBefore(slots[firstDisabled].section, anchor);
      setTargetSlot(firstDisabled);
      toggleSearchControls(false);
      if (searchStatusItem.get().indexOf(maxMessage) !== -1) {
        setStatus(searchStatusItem, 'info', '');
      }
    } else {
      toggleSearchControls(true);
      renderSearchResults([], 0);
      setStatus(searchStatusItem, 'info', maxMessage);
    }
  }

  function renderSearchResults(results, slotIndex) {
    if (!results.length) {
      searchResultsItem.set('');
      return;
    }

    var html = ['<div class="apollo-results">'];
    for (var i = 0; i < results.length; i++) {
      var fallbackName = fallbackLocationName(slotIndex);
      var title = getEnglishResultName(results[i], slotIndex);
      var description = getEnglishResultDisplay(results[i], fallbackName);
      html.push(
        '<button type="button" class="apollo-result" data-slot="' + slotIndex +
        '" data-result="' + i + '">' +
          '<strong>' + escapeHtml(title) + '</strong>' +
          '<span>' + escapeHtml(description) + '</span>' +
        '</button>'
      );
    }
    html.push('</div>');
    searchResultsItem.set(html.join(''));

    var root = searchResultsItem.$element[0];
    var buttons = root.querySelectorAll('.apollo-result');
    for (var j = 0; j < buttons.length; j++) {
      buttons[j].addEventListener('click', (function(button) {
        return function() {
          var resultIndex = parseInt(button.getAttribute('data-result'), 10);
          applySearchResult(parseInt(button.getAttribute('data-slot'), 10), results[resultIndex]);
        };
      })(buttons[j]));
    }
  }

  function lookupTimezoneForSlot(slot, successPrefix) {
    var lat = parseFloat(slot.lat.get());
    var lon = parseFloat(slot.lon.get());

    if (isNaN(lat) || lat < -90 || lat > 90 || isNaN(lon) || lon < -180 || lon > 180) {
      setStatus(searchStatusItem, 'error', slot.title + ' has invalid coordinates, so timezone detection cannot run.');
      return;
    }

    setStatus(searchStatusItem, 'info', 'Detecting timezone for ' + slot.title + '...');

    var xhr = new XMLHttpRequest();
    xhr.open(
      'GET',
      'https://timeapi.io/api/timezone/coordinate?latitude=' +
      lat.toFixed(6) +
      '&longitude=' +
      lon.toFixed(6)
    );
    xhr.onload = function() {
      if (xhr.status !== 200) {
        setStatus(searchStatusItem, 'error', 'Timezone detection failed. Please choose it manually.');
        return;
      }

      var data;
      try {
        data = JSON.parse(xhr.responseText);
      } catch (e) {
        setStatus(searchStatusItem, 'error', 'Timezone response could not be parsed.');
        return;
      }

      var standardSeconds = ((data.standardUtcOffset || {}).seconds || 0);
      slot.tz.set(String(Math.round(standardSeconds / 60)));
      slot.dst.set(!!data.isDayLightSavingActive);
      refreshSlot(slot);
      clearMessages();
      setStatus(
        searchStatusItem,
        'success',
        (successPrefix || slot.title) + ' updated to ' + (data.timeZone || 'the detected timezone') + '.'
      );
    };
    xhr.onerror = function() {
      setStatus(searchStatusItem, 'error', 'Network error while detecting timezone.');
    };
    xhr.send();
  }

  function applySearchResult(slotIndex, result) {
    var slot = getSlot(slotIndex);
    var lat = parseFloat(result.lat);
    var lon = parseFloat(result.lon);

    if (isNaN(lat) || isNaN(lon)) {
      setStatus(searchStatusItem, 'error', 'The selected result does not contain valid coordinates.');
      return;
    }

    fillSlot(slot, {
      name: getEnglishResultName(result, slot.index),
      lat: lat,
      lon: lon,
      baseOffset: parseInt(slot.tz.get(), 10) || 0,
      dst: !!slot.dst.get()
    });
    updateLayout();
    clearMessages();
    renderSearchResults([], slot.index);
    lookupTimezoneForSlot(slot, slot.title);
  }

  function performSearch() {
    var query = String(searchQueryItem.get() || '').trim();
    var slot = getSlot(editSlotItem.get());

    if (!query) {
      setStatus(searchStatusItem, 'error', 'Enter a place name to search.');
      renderSearchResults([], slot.index);
      return;
    }

    setStatus(searchStatusItem, 'info', 'Searching for places...');
    renderSearchResults([], slot.index);

    var xhr = new XMLHttpRequest();
    xhr.open(
      'GET',
      'https://nominatim.openstreetmap.org/search?q=' +
      encodeURIComponent(query) +
      '&format=json&limit=5&addressdetails=1&namedetails=1'
    );
    xhr.setRequestHeader('Accept-Language', 'en');
    xhr.onload = function() {
      if (xhr.status !== 200) {
        setStatus(searchStatusItem, 'error', 'Place search failed.');
        return;
      }

      var results;
      try {
        results = JSON.parse(xhr.responseText);
      } catch (e) {
        setStatus(searchStatusItem, 'error', 'Search results could not be parsed.');
        return;
      }

      if (!results.length) {
        setStatus(searchStatusItem, 'error', 'No matching places were found.');
        renderSearchResults([], slot.index);
        return;
      }

      setStatus(
        searchStatusItem,
        'success',
        'Found ' + results.length + ' candidate result(s). Tap one to fill ' + slot.title + '.'
      );
      renderSearchResults(results, slot.index);
    };
    xhr.onerror = function() {
      setStatus(searchStatusItem, 'error', 'Network error while searching.');
    };
    xhr.send();
  }

  function fillCurrentLocation() {
    if (!navigator.geolocation) {
      setStatus(searchStatusItem, 'error', 'This environment does not support geolocation.');
      return;
    }

    var slot = getSlot(editSlotItem.get());
    setStatus(searchStatusItem, 'info', 'Reading current location...');

    navigator.geolocation.getCurrentPosition(function(position) {
      fillSlot(slot, {
        name: toEnglishName(slot.name.get(), fallbackLocationName(slot.index)),
        lat: position.coords.latitude,
        lon: position.coords.longitude,
        baseOffset: parseInt(slot.tz.get(), 10) || 0,
        dst: !!slot.dst.get()
      });
      updateLayout();
      clearMessages();
      lookupTimezoneForSlot(slot, slot.title);
    }, function() {
      setStatus(searchStatusItem, 'error', 'Current location could not be read.');
    });
  }

  function collectLocations() {
    var locations = [];
    var errors = [];

    for (var i = 0; i < slots.length; i++) {
      var slot = slots[i];
      if (!slot.enabled.get()) {
        continue;
      }

      var name = String(slot.name.get() || '').trim();
      var englishName = toEnglishName(name, '');
      var lat = parseFloat(slot.lat.get());
      var lon = parseFloat(slot.lon.get());
      var baseOffset = parseInt(slot.tz.get(), 10);

      if (!englishName) {
        errors.push(slot.title + ' requires a location name in English/ASCII.');
        continue;
      }
      if (name !== englishName || !isAsciiText(name)) {
        errors.push(slot.title + ' name must use English/ASCII characters only.');
        continue;
      }
      if (isNaN(lat) || lat < -90 || lat > 90) {
        errors.push(slot.title + ' latitude must be between -90 and 90.');
        continue;
      }
      if (isNaN(lon) || lon < -180 || lon > 180) {
        errors.push(slot.title + ' longitude must be between -180 and 180.');
        continue;
      }
      if (isNaN(baseOffset) || baseOffset < -720 || baseOffset > 840) {
        errors.push(slot.title + ' timezone offset is out of range.');
        continue;
      }

      locations.push({
        name: englishName,
        lat: roundNumber(lat, 6),
        lon: roundNumber(lon, 6),
        baseOffset: baseOffset,
        dst: !!slot.dst.get()
      });
    }

    if (!locations.length) {
      errors.push('Enable and save at least one location.');
    }

    return {
      locations: locations,
      errors: errors
    };
  }

  function saveLocations() {
    var result = collectLocations();
    if (result.errors.length) {
      setStatus(saveStatusItem, 'error', result.errors[0]);
      return;
    }

    var payload = {
      LOCATIONS_JSON: JSON.stringify(result.locations)
    };

    hiddenLocationsItem.set(payload.LOCATIONS_JSON);
    setStatus(saveStatusItem, 'success', 'Saving and syncing to the watch...');
    window.location.href = (window.returnTo || 'pebblejs://close#') +
      encodeURIComponent(JSON.stringify(payload));
  }

  function buildSlot(index) {
    var slotNumber = index + 1;
    var prefix = 'LOC_' + slotNumber + '_';
    return {
      index: index,
      title: 'Location ' + slotNumber,
      heading: clay.getItemById('loc-' + slotNumber + '-heading'),
      enabled: clay.getItemByMessageKey(prefix + 'ENABLED'),
      name: clay.getItemByMessageKey(prefix + 'NAME'),
      lat: clay.getItemByMessageKey(prefix + 'LAT'),
      lon: clay.getItemByMessageKey(prefix + 'LON'),
      tz: clay.getItemByMessageKey(prefix + 'TZ'),
      dst: clay.getItemByMessageKey(prefix + 'DST'),
      lookupButton: clay.getItemById('loc-' + slotNumber + '-timezone-button'),
      clearButton: clay.getItemById('loc-' + slotNumber + '-clear-button'),
      summary: clay.getItemById('loc-' + slotNumber + '-summary'),
      section: getSectionElement(clay.getItemByMessageKey(prefix + 'ENABLED')),
      isAddSlot: false
    };
  }

  clay.on(clay.EVENTS.AFTER_BUILD, function() {
    injectStyles();
    slots = [];

    hiddenLocationsItem = clay.getItemByMessageKey('LOCATIONS_JSON');
    editSlotItem = clay.getItemByMessageKey('EDIT_SLOT');
    searchQueryItem = clay.getItemByMessageKey('SEARCH_QUERY');
    searchStatusItem = clay.getItemById('search-status');
    searchResultsItem = clay.getItemById('search-results');
    saveStatusItem = clay.getItemById('save-status');

    searchButtonItem = clay.getItemById('search-button');
    currentLocationButtonItem = clay.getItemById('current-location-button');
    saveButtonItem = clay.getItemById('save-button');
    rootFormEl = document.getElementById('main-form');
    searchSectionEl = getSectionElement(editSlotItem);

    editSlotItem.hide();

    for (var i = 0; i < MAX; i++) {
      slots.push(buildSlot(i));
    }

    for (var j = 0; j < slots.length; j++) {
      (function(slot) {
        slot.enabled.on('change', function() {
          updateLayout();
          clearMessages();
        });

        slot.name.on('change', function() {
          var englishName = toEnglishName(slot.name.get(), '');
          var shouldLookup = slot.isAddSlot &&
            !!englishName &&
            hasValidCoordinates(slot);
          if (slot.name.get() && slot.name.get() !== englishName) {
            slot.name.set(englishName);
          }
          if (slot.name.get()) {
            slot.enabled.set(true);
          }
          updateLayout();
          clearMessages();
          if (shouldLookup) {
            lookupTimezoneForSlot(slot, slot.title);
          }
        });

        slot.lat.on('change', function() {
          updateLayout();
          clearMessages();
        });

        slot.lon.on('change', function() {
          updateLayout();
          clearMessages();
        });

        slot.tz.on('change', function() {
          updateLayout();
          clearMessages();
        });

        slot.dst.on('change', function() {
          updateLayout();
          clearMessages();
        });

        slot.lookupButton.on('click', function() {
          lookupTimezoneForSlot(slot, slot.title);
        });

        slot.clearButton.on('click', function() {
          clearSlot(slot);
          updateLayout();
          clearMessages();
          setStatus(searchStatusItem, 'success', slot.title + ' was cleared.');
          renderSearchResults([], slot.index);
        });
      })(slots[j]);
    }

    searchButtonItem.on('click', performSearch);
    currentLocationButtonItem.on('click', fillCurrentLocation);
    saveButtonItem.on('click', saveLocations);

    searchQueryItem.$manipulatorTarget[0].addEventListener('keydown', function(event) {
      if (event.key === 'Enter' || event.keyCode === 13) {
        event.preventDefault();
        performSearch();
      }
    });

    setStatus(searchStatusItem, 'info', '');
    setStatus(saveStatusItem, 'info', '');
    renderSearchResults([], 0);
    hydrateFromLocations(readStoredLocations());
  });
};
/* jshint ignore:end */
