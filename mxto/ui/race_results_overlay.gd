class_name RaceResultsOverlay
extends Control

@onready var race_results_label: RichTextLabel = $Center/Panel/Margin/Columns/RaceResultsPanel/Margin/Scroll/RaceResultsText
@onready var grand_prix_label: RichTextLabel = $Center/Panel/Margin/Columns/GrandPrixPanel/Margin/Scroll/GrandPrixText
@onready var grand_prix_panel: PanelContainer = $Center/Panel/Margin/Columns/GrandPrixPanel

func set_results(race_text: String, grand_prix_text: String) -> void:
	if race_results_label != null:
		race_results_label.text = race_text
	if grand_prix_label != null:
		grand_prix_label.text = grand_prix_text
	if grand_prix_panel != null:
		grand_prix_panel.visible = grand_prix_text.strip_edges() != ""
