<map version="freeplane 1.6.0">
<!--To view this file, download free mind mapping software Freeplane from http://freeplane.sourceforge.net -->
<node TEXT="Ventilastation" FOLDED="false" ID="ID_134084202" CREATED="1565214232464" MODIFIED="1565214238904" STYLE="oval">
<font SIZE="18"/>
<hook NAME="MapStyle">
    <properties edgeColorConfiguration="#808080ff,#ff0000ff,#0000ffff,#00ff00ff,#ff00ffff,#00ffffff,#7c0000ff,#00007cff,#007c00ff,#7c007cff,#007c7cff,#7c7c00ff" fit_to_viewport="false"/>

<map_styles>
<stylenode LOCALIZED_TEXT="styles.root_node" STYLE="oval" UNIFORM_SHAPE="true" VGAP_QUANTITY="24.0 pt">
<font SIZE="24"/>
<stylenode LOCALIZED_TEXT="styles.predefined" POSITION="right" STYLE="bubble">
<stylenode LOCALIZED_TEXT="default" ICON_SIZE="12.0 pt" COLOR="#000000" STYLE="fork">
<font NAME="SansSerif" SIZE="10" BOLD="false" ITALIC="false"/>
</stylenode>
<stylenode LOCALIZED_TEXT="defaultstyle.details"/>
<stylenode LOCALIZED_TEXT="defaultstyle.attributes">
<font SIZE="9"/>
</stylenode>
<stylenode LOCALIZED_TEXT="defaultstyle.note" COLOR="#000000" BACKGROUND_COLOR="#ffffff" TEXT_ALIGN="LEFT"/>
<stylenode LOCALIZED_TEXT="defaultstyle.floating">
<edge STYLE="hide_edge"/>
<cloud COLOR="#f0f0f0" SHAPE="ROUND_RECT"/>
</stylenode>
</stylenode>
<stylenode LOCALIZED_TEXT="styles.user-defined" POSITION="right" STYLE="bubble">
<stylenode LOCALIZED_TEXT="styles.topic" COLOR="#18898b" STYLE="fork">
<font NAME="Liberation Sans" SIZE="10" BOLD="true"/>
</stylenode>
<stylenode LOCALIZED_TEXT="styles.subtopic" COLOR="#cc3300" STYLE="fork">
<font NAME="Liberation Sans" SIZE="10" BOLD="true"/>
</stylenode>
<stylenode LOCALIZED_TEXT="styles.subsubtopic" COLOR="#669900">
<font NAME="Liberation Sans" SIZE="10" BOLD="true"/>
</stylenode>
<stylenode LOCALIZED_TEXT="styles.important">
<icon BUILTIN="yes"/>
</stylenode>
</stylenode>
<stylenode LOCALIZED_TEXT="styles.AutomaticLayout" POSITION="right" STYLE="bubble">
<stylenode LOCALIZED_TEXT="AutomaticLayout.level.root" COLOR="#000000" STYLE="oval" SHAPE_HORIZONTAL_MARGIN="10.0 pt" SHAPE_VERTICAL_MARGIN="10.0 pt">
<font SIZE="18"/>
</stylenode>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,1" COLOR="#0033ff">
<font SIZE="16"/>
</stylenode>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,2" COLOR="#00b439">
<font SIZE="14"/>
</stylenode>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,3" COLOR="#990000">
<font SIZE="12"/>
</stylenode>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,4" COLOR="#111111">
<font SIZE="10"/>
</stylenode>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,5"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,6"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,7"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,8"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,9"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,10"/>
<stylenode LOCALIZED_TEXT="AutomaticLayout.level,11"/>
</stylenode>
</stylenode>
</map_styles>
</hook>
<hook NAME="AutomaticEdgeColor" COUNTER="8" RULE="ON_BRANCH_CREATION"/>
<node TEXT="Hardware" POSITION="right" ID="ID_1482261235" CREATED="1565214241660" MODIFIED="1565214250183">
<edge COLOR="#ff0000"/>
<node TEXT="Power" ID="ID_989984960" CREATED="1565214256313" MODIFIED="1565214269891">
<node TEXT="Mejorar autonom&#xed;a" ID="ID_1053793317" CREATED="1565214273053" MODIFIED="1565214342123"/>
<node TEXT="Medir autonom&#xed;a actual" ID="ID_716238077" CREATED="1565214633792" MODIFIED="1565214641341"/>
</node>
<node TEXT="Estructura" ID="ID_685846542" CREATED="1565214300536" MODIFIED="1565214303014">
<node TEXT="Construir Joystick" ID="ID_1504629736" CREATED="1565214305906" MODIFIED="1565214385574"/>
</node>
<node TEXT="Thermal" ID="ID_1126027362" CREATED="1565214312579" MODIFIED="1565214316593">
<node TEXT="Prueba temperatura 8hs" ID="ID_1238595437" CREATED="1565214317711" MODIFIED="1565214366690"/>
</node>
</node>
<node TEXT="Software" POSITION="right" ID="ID_812652133" CREATED="1565214251066" MODIFIED="1565214253956">
<edge COLOR="#0000ff"/>
<node TEXT="Juego" ID="ID_101731045" CREATED="1565214400297" MODIFIED="1565214511686">
<node TEXT="Disparar a naves" ID="ID_1961374509" CREATED="1565214937566" MODIFIED="1565214946174"/>
<node TEXT="Acercar el infinito (que no termine en px 0)" ID="ID_1642989884" CREATED="1565214959509" MODIFIED="1565215014720"/>
<node TEXT="Agrandar la profundidad de juego (m&#xe1;s de 127)" ID="ID_1143248935" CREATED="1565214986833" MODIFIED="1565215092997"/>
<node TEXT="Escenas" ID="ID_213294128" CREATED="1565214389789" MODIFIED="1565214399606"/>
<node TEXT="Reducir hitbox" ID="ID_1747283469" CREATED="1565214947972" MODIFIED="1565214958989"/>
<node TEXT="Niveles" ID="ID_920510348" CREATED="1565214997728" MODIFIED="1565215100323"/>
</node>
<node TEXT="Telemetr&#xed;a" ID="ID_298769282" CREATED="1565214522422" MODIFIED="1565214539361">
<node TEXT="Influx en alpine, o volvemos a raspbian?" ID="ID_1529334404" CREATED="1565214541181" MODIFIED="1565215116601"/>
<node TEXT="alarmas" ID="ID_1041298377" CREATED="1565215132298" MODIFIED="1565216231950"/>
<node TEXT="Corte por temperatura" ID="ID_155663321" CREATED="1565216261351" MODIFIED="1565216267693"/>
<node TEXT="Corte por velocidad" ID="ID_1892938133" CREATED="1565216269283" MODIFIED="1565216275883"/>
</node>
<node TEXT="Joystick/Base" ID="ID_875902883" CREATED="1565214565033" MODIFIED="1565216249232">
<node TEXT="Usar libinput en vez de pyglet" ID="ID_1701686858" CREATED="1565214568459" MODIFIED="1565214665500"/>
<node TEXT="Usar pyglet para audio sin X ?" ID="ID_558925920" CREATED="1565214667331" MODIFIED="1565214919311"/>
</node>
</node>
</node>
</map>
