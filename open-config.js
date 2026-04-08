#!/usr/bin/env node
// 生成配置页预览并在浏览器打开，用于本地开发调试。
// 用法: node open-config.js
//
// 不依赖 Clay 框架：直接提供 fake Clay API 并调用 customFn，
// 生成完全独立的 HTML 文件，可在任何浏览器中打开。

var fs = require('fs');
var os = require('os');
var path = require('path');
var execSync = require('child_process').execSync;

// 读取 customFn 源码（去掉 module.exports = 包装）
var customFnSrc = fs.readFileSync(
  path.join(__dirname, 'src/pkjs/customFn.js'), 'utf8'
);
// 提取函数体：去掉首尾的 jshint 注释和 module.exports = 行
customFnSrc = customFnSrc
  .replace(/\/\* jshint ignore:start \*\/\s*/g, '')
  .replace(/\/\* jshint ignore:end \*\/\s*/g, '')
  .replace(/^module\.exports\s*=\s*/, '');

// 默认地点（与 config.js 保持一致）
var DEFAULT_LOCATIONS = JSON.stringify([
  { name: 'London',   lat:  51.5074, lon:  -0.1278, baseOffset:    0, dst: false },
  { name: 'New York', lat:  40.7128, lon: -74.0060, baseOffset: -300, dst: false },
  { name: 'Tokyo',    lat:  35.6762, lon: 139.6503, baseOffset:  540, dst: false },
  { name: 'Sydney',   lat: -33.8688, lon: 151.2093, baseOffset:  600, dst: false }
]);

var html = '<!DOCTYPE html>\n<html lang="zh-CN"><head><meta charset="utf-8">\n' +
  '<meta name="viewport" content="width=device-width,initial-scale=1">\n' +
  '<title>Apollo 设置预览</title>\n</head>\n<body>\n<script>\n' +
  '// Fake Clay API — 提供 customFn 所需的最小接口\n' +
  'var fakeClay = {\n' +
  '  getItemsByMessageKey: function(key) {\n' +
  '    if (key === "LOCATIONS_JSON") {\n' +
  '      var stored = localStorage.getItem("clay-settings");\n' +
  '      var val = ' + DEFAULT_LOCATIONS + ';\n' +
  '      if (stored) { try { val = JSON.parse(JSON.parse(stored).LOCATIONS_JSON || "[]"); } catch(e){} }\n' +
  '      return [{ get: function() { return JSON.stringify(val); } }];\n' +
  '    }\n' +
  '    return [];\n' +
  '  }\n' +
  '};\n\n' +
  '// 调用 customFn\n' +
  '(' + customFnSrc + ').call(fakeClay);\n' +
  '</script>\n</body>\n</html>\n';

var tmpFile = path.join(os.tmpdir(), 'apollo-config.html');
fs.writeFileSync(tmpFile, html, 'utf8');

console.log('\n配置页已生成，正在浏览器中打开…');
console.log('临时文件: ' + tmpFile + '\n');

execSync('open ' + JSON.stringify(tmpFile));
