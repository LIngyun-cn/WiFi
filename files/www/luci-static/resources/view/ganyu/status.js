'use strict';
'require view';
'require poll';
'require fs';

return view.extend({
	load: function() {
		return L.resolveDefault(fs.exec_direct('/usr/bin/ganyu-status'), '');
	},

	render: function(data) {
		var out = E('pre', {
			'style': 'font-family:monospace;font-size:smaller;white-space:pre-wrap;line-height:1.6;margin:0'
		}, [ data || '' ]);

		poll.add(function() {
			return fs.exec_direct('/usr/bin/ganyu-status').then(function(txt) {
				out.textContent = txt;
			}).catch(function() {});
		}, 5);

		return E([], [
			E('h2', {}, _('反检测状态')),
			E('div', { 'class': 'cbi-section' }, [ out ])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
