<script lang="ts">
	import { default as Button, Icon } from '@smui/button';
	import { scale } from 'svelte/transition';
	import { expoInOut } from 'svelte/easing';

	import type { SearchResultItemType } from './store';

	export let searchResultItem: SearchResultItemType;
</script>

<section class="item" transition:scale={{ duration: 1000, easing: expoInOut }}>
	<div>{searchResultItem.snippet}</div>
</section>
