" General
    syntax on
    set number
    set whichwrap+=<,>,h,l,[,]  " EOL wrapping
    set cursorline  " horizontal line under selected line
    set backspace=indent,eol,start



" Indentation
    set expandtab      " tabs to spaces
    set tabstop=4      " tab width = 4
    set shiftwidth=4   " indentation size = 4
    set smartindent    " auto-indent
    set autoindent     " fix indentation
    set softtabstop=4  " backspace deletes 4 spaces



" Key bindings
    let mapleader="."
    
    " .n toggles line numbers
    nnoremap <leader>n :set number!<CR>
    
    " H goes to beginning of line
    nnoremap H 0

    " J goes to bottom of file
    nnoremap J G

    " K goes to top of file
    nnoremap K gg

    " L goes to end of line
    nnoremap L $



" Return to the line you were on last time
    augroup line_return
        au!
        au BufReadPost *
            \ if line("'\"") > 0 && line("'\"") <= line("$") |
            \     execute 'normal! g`"zvzz' |
            \ endif
    augroup END
