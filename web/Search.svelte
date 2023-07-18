<script lang="ts">
	import Button from '@smui/button';
	import IconButton, { Icon } from '@smui/icon-button';
	import SearchResultItem from './SearchResultItem.svelte';
	import type { SearchResultItemType } from './store';
	import { SearchTypesense } from './store';
	let searchTerm: string = '';

	// Query results
	let searchResults: SearchResultItemType[] = [];
	const xhr = new XMLHttpRequest();
	const searchTypesense = async () => {
		searchResults = await SearchTypesense(searchTerm);
	};
</script>

<section id="query-section">
	<div id="search-input-cont">
		<input
			type="text"
			id="search-field"
			placeholder="Enter Search Term"
			autocomplete="off"
			bind:value={searchTerm}
			on:input={searchTypesense}
		/>
	</div>
</section>

<main id="search-results">
	{#if !searchTerm || searchResults.length === 0}
		<div> No Results </div>
	{:else}
		{#each searchResults as searchResultItem}
			<SearchResultItem {searchResultItem} />
		{/each}
	{/if}
</main>

<style>
	* {
		box-sizing: border-box;
	}

	#query-section {
		width: 100%;
		display: flex;
		justify-content: center;
		align-items: center;
		padding: 2% 0;
	}

	#search-input-cont {
		width: 40%;
		display: flex;
		align-items: center;
		margin: 0 0 0 10px;
	}

	#search-field {
		width: 100%;
		font-size: 1.3rem;
		border: 1px solid gray;
		border-radius: 5px;
		padding: 8px;
		margin: 0 10px 0;
	}
	#search-results {
		display: grid;
		grid-auto-flow: row;
		grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
		/* grid-template-rows: repeat(auto-fit, minmax(260px, 1fr)); */
		grid-auto-rows: minmax(260px, 1.3fr);

	}
</style>
