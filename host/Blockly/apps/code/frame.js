(function() {
  // Load the language-specific files listed on the URL.
  var files =
	  decodeURIComponent(window.location.search.substring(1)).split('&');
  var headEl = document.getElementsByTagName('head')[0];
  for (var x = 0; x < files.length; x++) {
	var file = files[x];
	if (file.match(/^\w+\/\w+\.js$/)) {
		var scriptEl = document.createElement("script");
		var typeAttr = document.createAttribute("type");
		typeAttr.nodeValue = "text/javascript";
		scriptEl.setAttributeNode(typeAttr);
		var srcAttr = document.createAttribute("src");
		srcAttr.nodeValue = "../../language/" + file;
		scriptEl.setAttributeNode(srcAttr);
		headEl.appendChild(scriptEl); 
	  /*
	  document.writeln('<script type="text/javascript" ' +
		  'src="../../language/' + file + '"><' + '/script>');
	  */
	} else {
	  console.error('Illegal language file: ' + file);
	}
  }
})();

function init() {
  var toolbox = null;
  if (window.parent.document) {
	toolbox = window.parent.document.getElementById('toolbox');
  }
  Blockly.inject(document.body, {path: '../../', toolbox: toolbox});

  if (window.parent.init) {
	// Let the top-level application know that Blockly is ready.
	window.parent.init(Blockly); 
  } else {
	// Attempt to diagnose the problem.
	var msg = 'Error: Unable to communicate between frames.\n\n';
	if (window.parent == window) {
	  msg += 'Try loading index.html instead of frame.html';
	} else if (window.location.protocol == 'file:') {
	  msg += 'This may be due to a security restriction preventing\n' +
		  'access when using the file:// protocol.\n' +
		  'http://code.google.com/p/chromium/issues/detail?id=47416';
	}
	alert(msg);
  }
}

document.addEventListener(
	'DOMContentLoaded', 
	function () 
	{
		init();
	}
);