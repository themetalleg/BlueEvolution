install(FILES BlueEvolution/index.theme LICENSE
	DESTINATION ${privdatadir}/icons/BlueEvolution
)

include(aliases.cmake)
foreach(group IN LISTS bootstrap_icon_groups)
	string(REPLACE "|" ";" mapping "${group}")
	list(GET mapping 0 source_icon)
	list(REMOVE_AT mapping 0)
	foreach(alias IN LISTS mapping)
		foreach(suffix "" "-symbolic")
			install(FILES svg/${source_icon}.svg
				DESTINATION ${privdatadir}/icons/BlueEvolution/scalable/actions
				RENAME ${alias}${suffix}.svg
			)
		endforeach()
	endforeach()
endforeach()