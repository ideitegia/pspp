;;; pspp-mode-el -- Major mode for editing PSPP files


;; Author: John Darrington <john@darrington.wattle.id.au>
;; Created: 05 March 2005
;; Keywords: PSPP major-mode

;; Copyright (C) 2005 John Darrington
;; 
;; Based on the example wpdl-mode.el by Scott Borton
;; Author: Scott Andrew Borton <scott@pp.htv.fi>

;; Copyright (C) 2000, 2003 Scott Andrew Borton <scott@pp.htv.fi>

;; This program is free software; you can redistribute it and/or
;; modify it under the terms of the GNU General Public License as
;; published by the Free Software Foundation; either version 2 of
;; the License, or (at your option) any later version.

;; This program is distributed in the hope that it will be
;; useful, but WITHOUT ANY WARRANTY; without even the implied
;; warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
;; PURPOSE.  See the GNU General Public License for more details.

;; You should have received a copy of the GNU General Public
;; License along with this program; if not, write to the Free
;; Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
;; MA 02110-1301 USA

;;; Code:
(defvar pspp-mode-hook nil)
(defvar pspp-mode-map
  (let ((pspp-mode-map (make-keymap)))
    (define-key pspp-mode-map "\C-j" 'newline-and-indent)
    pspp-mode-map)
  "Keymap for PSPP major mode")

(add-to-list 'auto-mode-alist '("\\.sps\\'" . pspp-mode))


(defun pspp-data-block ()
  "Returns t if current line is inside a data block."
  (interactive)

  (let (
	(end-data-found nil) 
	(begin-data-found nil) 
	(inside-block nil)
	)

    (save-excursion 
      (beginning-of-line)
      (while (not (or (bobp) end-data-found)  )

	(if (looking-at "^[ \t]*END +DATA\.") 
	    (setq end-data-found t)
	  )

	(forward-line -1)

	(if (looking-at "^[ \t]*BEGIN +DATA\.") 
	    (setq begin-data-found t)
	  )
	  
	)
      )

    (setq inside-block (and begin-data-found (not end-data-found)))

    inside-block
    )
  
  )


(defun pspp-indent-line ()
  "Indent current line as PSPP code."
  (interactive)
  (beginning-of-line)
  (let (
	(verbatim nil)
  	(the-indent 0)		; Default indent to column 0
	)

    (if (bobp)
	(setq the-indent 0)	; First line is always non-indented
      )
  

    (let (
	  (within-command nil)
	  (blank-line t)
	  )

      ;; If the most recent non blank line ended with a `.' then
      ;; we're at the start of a new command.

      (save-excursion 
	(while (and blank-line  (not (bobp)))
	  (forward-line -1)
	
	  (if (and (not (pspp-data-block)) (not (looking-at "^[ \t]*$")))
	      (progn 
 	      (setq blank-line nil)
	    
	      (if (not (looking-at ".*\\.[ \t]*$"))
		(setq within-command t)
	      )
	      )
	  )
	  )
	
      (if within-command (setq the-indent 8) )
      )
  
      )

    (if (not (pspp-data-block))  (indent-line-to the-indent))
))



(defvar pspp-mode-syntax-table
  (let (
	(x-pspp-mode-syntax-table (make-syntax-table))
	)
	
    (modify-syntax-entry ?_  "w" x-pspp-mode-syntax-table)
    (modify-syntax-entry ?.  "w" x-pspp-mode-syntax-table)
    (modify-syntax-entry ?\" "|" x-pspp-mode-syntax-table)
    (modify-syntax-entry ?\' "|" x-pspp-mode-syntax-table)

    ;; Comment definitions
      (modify-syntax-entry ?*  "<\n" x-pspp-mode-syntax-table)

;;    (modify-syntax-entry ?\n "- 1"  x-pspp-mode-syntax-table)
;;    (modify-syntax-entry ?*  ". 2"  x-pspp-mode-syntax-table)

    (modify-syntax-entry ?\n ">*"  x-pspp-mode-syntax-table)

    x-pspp-mode-syntax-table)
  
    "Syntax table for pspp-mode")
  
(defun pspp-mode ()
  (interactive)
  (kill-all-local-variables)
  (use-local-map pspp-mode-map)
  (set-syntax-table pspp-mode-syntax-table)
  (setq comment-start "* ")
  ;; Register our indentation function
  (set (make-local-variable 'indent-line-function) 'pspp-indent-line)  
  (setq major-mode 'pspp-mode)
  (setq mode-name "PSPP")
  (run-hooks 'pspp-mode-hook))

(provide 'pspp-mode)

;;; pspp-mode.el ends here

