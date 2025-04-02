import {downloadSnapshotUrlFor, IASearchResult, searchForSpectrum} from "./ia-search";
import * as _ from "lodash";
import {Spectrum} from "./spectrum";

function updateUrl(updateMap: Record<string, string>) {
    const parsedQuery = new URLSearchParams(window.location.search);
    for (const item of Object.keys(updateMap)) {
        parsedQuery.set(item, updateMap[item]);
    }
    const url = new URL(window.location.toString());
    url.search = parsedQuery.toString();
    window.history.pushState({}, undefined, url.toString());
}

export function setupLoadGame(spectrum: Spectrum, loadGameDialog: HTMLDialogElement) {
    const $searchResults = loadGameDialog.querySelector(".search-results");
    const $template = loadGameDialog.querySelector("template").content;

    function updateSearchResults(results: IASearchResult[]) {
        $searchResults.innerHTML = "";
        if (results.length === 0) {
            $searchResults.innerHTML = "<h3>No results</h3>";
        }
        for (const result of results) {
            const newItem = $template.cloneNode(true) as HTMLElement;
            newItem.querySelector(".description").innerHTML = result.title;
            newItem.querySelector(".description").addEventListener("click", async (evt) => {
                evt.preventDefault();
                loadGameDialog.close();
                const z80FileUrl = new URL(await downloadSnapshotUrlFor(result.identifier));
                await spectrum.loadSnapshot(z80FileUrl);
                updateUrl({load: z80FileUrl.toString()});
            });
            $searchResults.appendChild(newItem);
        }
    }

    const $searchButton: HTMLInputElement = loadGameDialog.querySelector(".search-input");

    async function doSearch() {
        $searchResults.innerHTML = "<h5>Loading...</h5>";
        const results = await searchForSpectrum($searchButton.value);
        updateSearchResults(results);
    }

    const debouncedSearch = _.debounce(doSearch, 500);
    $searchButton.addEventListener("change", debouncedSearch);
    $searchButton.addEventListener("keyup", debouncedSearch);
}
