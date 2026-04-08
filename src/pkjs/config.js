var MAX_LOCATIONS = 8;

var DEFAULT_LOCATIONS = [
  { name: 'London',   lat:  51.5074,  lon:  -0.1278, baseOffset:    0, dst: false },
  { name: 'New York', lat:  40.7128,  lon: -74.0060, baseOffset: -300, dst: false },
  { name: 'Tokyo',    lat:  35.6762,  lon: 139.6503, baseOffset:  540, dst: false },
  { name: 'Sydney',   lat: -33.8688,  lon: 151.2093, baseOffset:  600, dst: false }
];

var DEFAULT_LOCATIONS_JSON = JSON.stringify(DEFAULT_LOCATIONS);

function pad2(num) {
  return num < 10 ? '0' + num : '' + num;
}

function buildTimezoneOptions() {
  var options = [];
  for (var minutes = -720; minutes <= 840; minutes += 15) {
    var absMinutes = Math.abs(minutes);
    options.push({
      value: String(minutes),
      label: 'UTC' +
        (minutes < 0 ? '-' : '+') +
        pad2(Math.floor(absMinutes / 60)) +
        ':' +
        pad2(absMinutes % 60)
    });
  }
  return options;
}

var TIMEZONE_OPTIONS = buildTimezoneOptions();

function buildSlotOptions() {
  var options = [];
  for (var i = 0; i < MAX_LOCATIONS; i++) {
    options.push({
      value: String(i),
      label: 'Location ' + (i + 1)
    });
  }
  return options;
}

var SLOT_OPTIONS = buildSlotOptions();

function buildLocationSection(index) {
  var slot = DEFAULT_LOCATIONS[index] || null;
  var slotNumber = index + 1;
  var prefix = 'LOC_' + slotNumber + '_';

  return {
    type: 'section',
    items: [
      {
        type: 'heading',
        id: 'loc-' + slotNumber + '-heading',
        defaultValue: 'Location ' + slotNumber
      },
      {
        type: 'toggle',
        messageKey: prefix + 'ENABLED',
        label: 'Enable this location',
        defaultValue: !!slot
      },
      {
        type: 'input',
        messageKey: prefix + 'NAME',
        label: 'Location name',
        defaultValue: slot ? slot.name : '',
        attributes: {
          maxlength: 23,
          placeholder: 'Example: Tokyo'
        }
      },
      {
        type: 'input',
        messageKey: prefix + 'LAT',
        label: 'Latitude',
        defaultValue: slot ? String(slot.lat) : '',
        attributes: {
          type: 'number',
          step: '0.0001',
          min: '-90',
          max: '90',
          placeholder: 'Example: 35.6762'
        }
      },
      {
        type: 'input',
        messageKey: prefix + 'LON',
        label: 'Longitude',
        defaultValue: slot ? String(slot.lon) : '',
        attributes: {
          type: 'number',
          step: '0.0001',
          min: '-180',
          max: '180',
          placeholder: 'Example: 139.6503'
        }
      },
      {
        type: 'select',
        messageKey: prefix + 'TZ',
        label: 'Standard UTC offset',
        defaultValue: slot ? String(slot.baseOffset) : '0',
        options: TIMEZONE_OPTIONS
      },
      {
        type: 'toggle',
        messageKey: prefix + 'DST',
        label: 'Daylight saving time',
        defaultValue: slot ? !!slot.dst : false
      },
      {
        type: 'button',
        id: 'loc-' + slotNumber + '-timezone-button',
        defaultValue: 'Detect timezone from coordinates'
      },
      {
        type: 'button',
        id: 'loc-' + slotNumber + '-clear-button',
        defaultValue: 'Clear this location'
      },
      {
        type: 'text',
        id: 'loc-' + slotNumber + '-summary',
        defaultValue: ''
      }
    ]
  };
}

var config = [
  {
    type: 'heading',
    defaultValue: 'Apollo',
    size: 1
  },
  {
    type: 'text',
    defaultValue: 'Manage up to 8 locations on the phone and sync them to the watch. The watch calculates sunrise, sunset, and related solar events offline.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Search and quick fill'
      },
      {
        type: 'text',
        defaultValue: 'Search for a new place or use your current position. Results fill the next available slot automatically, and you can still edit the values manually before saving.'
      },
      {
        type: 'select',
        messageKey: 'EDIT_SLOT',
        label: 'Target slot',
        defaultValue: '0',
        options: SLOT_OPTIONS
      },
      {
        type: 'input',
        messageKey: 'SEARCH_QUERY',
        label: 'Place search',
        defaultValue: '',
        attributes: {
          placeholder: 'Enter a city, region, or landmark'
        }
      },
      {
        type: 'button',
        id: 'search-button',
        primary: true,
        defaultValue: 'Search places'
      },
      {
        type: 'button',
        id: 'current-location-button',
        defaultValue: 'Use current location'
      },
      {
        type: 'text',
        id: 'search-status',
        defaultValue: ''
      },
      {
        type: 'text',
        id: 'search-results',
        defaultValue: ''
      }
    ]
  }
];

for (var i = 0; i < MAX_LOCATIONS; i++) {
  config.push(buildLocationSection(i));
}

config.push(
  {
    type: 'input',
    messageKey: 'LOCATIONS_JSON',
    defaultValue: DEFAULT_LOCATIONS_JSON,
    attributes: {
      type: 'hidden'
    }
  },
  {
    type: 'button',
    id: 'save-button',
    primary: true,
    defaultValue: 'Save and sync to watch'
  },
  {
    type: 'text',
    id: 'save-status',
    defaultValue: ''
  }
);

module.exports = config;
module.exports.MAX_LOCATIONS = MAX_LOCATIONS;
module.exports.DEFAULT_LOCATIONS = DEFAULT_LOCATIONS;
module.exports.DEFAULT_LOCATIONS_JSON = DEFAULT_LOCATIONS_JSON;
module.exports.TIMEZONE_OPTIONS = TIMEZONE_OPTIONS;
