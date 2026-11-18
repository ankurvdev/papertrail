import { writable, type Writable } from "svelte/store"

export interface SearchResultItemType {
    snippet: string
    content: string
    url: string
    filename: string
}

const EmptySearchResultItemType: SearchResultItemType = {
    snippet: "",
    content: "",
    url: "",
    filename: ""
}

export const vm_selectedIds: Writable<{ [item_id: string]: boolean }> = writable({})

export async function SearchTypesense(searchTerm: string): Promise<SearchResultItemType[]> {
    let searchParameters = {
        'q': searchTerm
    }
    const response = await fetch('/api/searcher/search?' + new URLSearchParams(searchParameters))
    const searchResults = await response.json();
    let results: SearchResultItemType[] = []
    if (searchResults && searchResults.length > 0) {
        searchResults.forEach((searchResult) => {
            let searchResultItem: SearchResultItemType = { ...EmptySearchResultItemType }
            searchResultItem.snippet = searchResult.path.split('/').reverse()[0];
            Object.assign(searchResultItem, searchResult);
            results.push(searchResultItem)
        });
    }
    return results
};
