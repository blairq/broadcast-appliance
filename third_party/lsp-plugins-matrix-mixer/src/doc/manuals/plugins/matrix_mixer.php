<?php
	plugin_header();
	$m      =   (strpos($PAGE, '_mono') > 0) ? 'm' : 's';
	$nc     =   (strpos($PAGE, '_x8') > 0) ? '8' : '4';
?>

<p>
	This plugin is a matrix mixer of <?= $nc ?> <?= ($m == 'm') ? 'mono' : 'stereo' ?> inputs and
	<?= $nc ?> <?= ($m == 'm') ? 'mono' : 'stereo' ?> outputs.
</p>
<p>
	Each input can be routed to each output with its own gain: the plugin has one control for every
	crosspoint of the matrix, so any output can be an individual mix of any inputs. It is useful to build
	monitor mixes, submixes and signal splits inside one plugin instance.
</p>
<?php if ($m == 's') { ?>
<p>
	Each crosspoint of the stereo version routes the left channel of the input to the left channel of the output
	and the right channel of the input to the right channel of the output with the same gain.
</p>
<?php } ?>
<p>
	By default the matrix is the identity one: each input is passed only to the output with the same number,
	all other crosspoints are muted. When the plugin is bypassed, it behaves the same way.
</p>
<p>
	All the gain changes are applied smoothly to avoid clicks. Muted crosspoints do not take any processing time.
</p>

<p><b>Main controls:</b></p>
<ul>
	<li>
		<b>Bypass</b> - bypass switch, when turned on (led indicator is shining), each output signal is similar
		to the input signal with the same number. That does not mean that the plugin is not working.
	</li>
</ul>
<p><b>Input controls:</b></p>
<ul>
	<li><b>Input label</b> - custom user-defined editable label of the input.</li>
	<li><b>S</b> - solo the input: when any input is soloed, all inputs that are not soloed are muted.</li>
	<li><b>M</b> - mute the input.</li>
	<li><b>P</b> - invert the phase of the input.</li>
	<li><b>Gain</b> - the gain applied to the input before it is routed to the outputs.</li>
	<li><b>Meter</b> - the level of the input signal after applying the gain.</li>
</ul>
<p><b>Matrix controls:</b></p>
<ul>
	<li>
		<b>Crosspoint</b> - the gain of the input of the row routed to the output of the column.
		The lowest value of the knob mutes the crosspoint. Double click on the knob toggles the crosspoint:
		an audible crosspoint gets muted, a muted one gets back its last gain (0 dB if it never had one).
	</li>
</ul>
<p><b>Output controls:</b></p>
<ul>
	<li><b>Output label</b> - custom user-defined editable label of the output.</li>
	<li><b>M</b> - mute the output.</li>
	<?php if ($m == 's') { ?>
	<li><b>Balance</b> - the balance between left and right channels of the output.</li>
	<?php } ?>
	<li><b>Gain</b> - the overall gain of the output.</li>
	<li><b>Meter</b> - the level of the output signal.</li>
</ul>
