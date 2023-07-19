//import adapter from '@sveltejs/adapter-auto'
import adapter from '@sveltejs/adapter-static'
import preprocess from 'svelte-preprocess'

/** @type {import('@sveltejs/kit').Config} */
const config = {
    // Consult https://github.com/sveltejs/svelte-preprocess
    // for more information about preprocessors
    preprocess: preprocess(),

    kit: {
        paths: {
            base: 'zzzsSvelte.BasePathzzze',
        },
        adapter: adapter({
            pages: 'dist',
            assets: 'dist'
        })
    }
}

export default config
