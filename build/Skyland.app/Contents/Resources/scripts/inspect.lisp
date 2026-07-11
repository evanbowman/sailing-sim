;;;
;;; inspect.lisp
;;;
;;; The game invokes the result of this script when the player picks the inspect
;;; option from the select menu.
;;;

(tr-bind-current)

(lambda (isle x y)
  (if-let ((info (room-load isle x y)))
      (let ((path (format "/scripts/inspect/%.lisp" (car info))))
        (when (file-exists? path)
          (eval-file path)))
    (dialog (tr "There's nothing remarkable here..."))))
