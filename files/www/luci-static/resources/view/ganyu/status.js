'use strict';
'require view';
'require poll';
'require fs';
'require ui';

/* 反检测体检页
 *
 * 数据全来自 /usr/bin/ganyu-status（纯文本，16 项判定 + 事件）。
 * 这里只负责上色和按钮：判定逻辑一律留在 shell 里，页面不做任何猜测。
 */

function style_of(line) {
	if (/^\[FAIL\]/.test(line)) return 'color:#d0021b;font-weight:600';
	if (/^\[WARN\]/.test(line)) return 'color:#c47f00;font-weight:600';
	if (/^\[PASS\]/.test(line)) return 'color:#0a7d24';
	if (/^\[ NA \]/.test(line)) return 'color:#888';
	if (/^#{5,}/.test(line))  return 'color:#d0021b;font-weight:700';
	if (/^[─━]{5,}/.test(line)) return 'color:#bbb';
	if (/^(反检测体检)/.test(line)) return 'font-weight:600';
	if (/^最近事件$/.test(line)) return 'font-weight:600;margin-top:6px';
	return '';
}

return view.extend({
	load: function() {
		return L.resolveDefault(fs.exec_direct('/usr/bin/ganyu-status'), '');
	},

	paint: function(host, text) {
		while (host.firstChild)
			host.removeChild(host.firstChild);

		String(text || '').split('\n').forEach(function(line) {
			var st = style_of(line);
			host.appendChild(E('div', {
				'style': 'font-family:monospace;font-size:12px;white-space:pre-wrap;' +
					'line-height:1.55;' + st
			}, [ line.length ? line : ' ' ]));
		});
	},

	render: function(data) {
		var self = this;
	var host = E('div', { 'class': 'cbi-section' });
		var title = E('h2', {}, [ _('反检测状态') ]);
		var btn = E('button', {
			'class': 'btn cbi-button cbi-button-apply',
			'click': ui.createHandlerFn(this, function() {
				btn.disabled = true;
				return fs.exec('/usr/bin/ganyu-rearm').then(function(res) {
					ui.addNotification(null, E('p', {}, [
						_('已请求恢复：'), (res && res.stdout) ? res.stdout.trim() : _('（无输出）')
					]), 'info');
				}).catch(function(err) {
					ui.addNotification(null, E('p', {}, [
						_('恢复失败：'), err.message || String(err)
					]), 'danger');
				}).finally(function() {
					btn.disabled = false;
					return fs.exec_direct('/usr/bin/ganyu-status').then(function(txt) {
						self.paint(host, txt);
					}).catch(function() {});
				});
			})
		}, [ _('恢复（重新启用 WAN）') ]);

		self.paint(host, data);

		/* 同一个 ganyu-status 一次要 fork 五六十个命令、耗时 4 秒左右；如果
		 * 上一次还没回来就再发一次，rpcd 里会排队，页面就卡在"正在载入"。
		 * 所以：30 秒一次，而且上一轮没结束就跳过本轮。 */
		var busy = false;

		poll.add(function() {
			if (busy)
				return;

			busy = true;

			return fs.exec_direct('/usr/bin/ganyu-status').then(function(txt) {
				self.paint(host, txt);
			}).catch(function() {
			}).finally(function() {
				busy = false;
			});
		}, 30);

		var check_out = E('pre', {
			'style': 'display:none;font-family:monospace;font-size:12px;white-space:pre-wrap;' +
				'margin:10px 0 0 0;padding:8px;background:#f7f7f7;border:1px solid #ddd'
		});

		var btn2 = E('button', {
			'class': 'btn cbi-button',
			'style': 'margin-left:8px',
			'click': ui.createHandlerFn(this, function() {
				btn2.disabled = true;
				check_out.style.display = '';
				check_out.textContent = _('正在抓包 3 秒并做主动探针…');
				return fs.exec('/usr/bin/ganyu-check').then(function(res) {
					check_out.textContent = (res && res.stdout) ? res.stdout : _('（无输出）');
				}).catch(function(err) {
					check_out.textContent = _('体检失败：') + (err.message || String(err));
				}).finally(function() { btn2.disabled = false; });
			})
		}, [ _('重新体检（抓包 3 秒）') ]);

		return E([], [
			title,
			E('div', { 'style': 'margin:6px 0 10px 0' }, [ btn, btn2 ]),
			check_out,
			host
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
