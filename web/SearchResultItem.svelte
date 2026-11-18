<script lang="ts">
	import { scale } from 'svelte/transition';
	import { expoInOut } from 'svelte/easing';
	import type { SearchResultItemType } from './store';
	export let searchResultItem: SearchResultItemType;
</script>

<div>
	<div class="item">
		<a class="mdc-typography--body2" href="{searchResultItem.url}">{searchResultItem.filename}</a>
		<div class="mdc-typography--caption" bind:innerHTML={searchResultItem.snippet} contenteditable />
	</div>
</div>
