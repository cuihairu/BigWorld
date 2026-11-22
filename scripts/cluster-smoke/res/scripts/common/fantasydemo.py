"""Cluster-smoke personality module for the fantasydemo placeholder assets.

The R4 asset tree (programming/fantasydemo/res) carries entity scripts but no
personality module. ScriptApp::initPersonality imports the module named by the
bw.xml <personality> setting ('fantasydemo'), which the engine resolves through
sys.path. The engine puts <res>/scripts/common on sys.path, so this module lives
in the cluster-smoke overlay's scripts/common directory rather than the asset
tree itself - it is bootstrap scaffolding for the cluster black-box regression
chain, not part of the placeholder assets.

A minimal personality is enough: ScriptApp reads the registered script-event
attributes off the module and skips the ones that are absent (ScriptErrorClear),
and callOnInit calls the module-level onInit(isReload).
"""

def onInit( isReload ):
	"""Called by Personality::callOnInit once after the module loads."""
	pass
