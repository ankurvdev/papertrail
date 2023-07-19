<script lang="ts">
	import { default as Button, Icon } from '@smui/button';
	import { scale } from 'svelte/transition';
	import { expoInOut } from 'svelte/easing';
	import type { SearchResultItemType } from './store';
	export let searchResultItem: SearchResultItemType;
</script>

<section class="item" transition:scale={{ duration: 1000, easing: expoInOut }}>
	<a href="{searchResultItem.url}">{searchResultItem.filename}</a>
	<div bind:innerHTML={searchResultItem.snippet} contenteditable />
</section>
<style>
.item {
	display: grid;
}
</style>