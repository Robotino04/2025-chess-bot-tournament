$
                 "Weights " & w_i \
                    "Bias " & b \
                  "Inputs " & x_i \
              "Activation " & f(z) \
  "Unactivated Prediction " & z = sum_i w_i dot x_i + b \
              "Prediction " & hat(y) = f(z) \
                    "Loss " & L = 1/2 (hat(y) - y)^2 \
$

Derivation for gradient of weights:
$
  L & = 1/2 (hat(y) - y)^2 \
  (diff L)/(diff w_1) & = (hat(y) - y) dot diff/(diff w_1) (f(z) - y)\
  (diff L)/(diff w_1) & = (hat(y) - y) dot f'(z) dot diff/(diff w_1) (sum_i w_i dot x_i + b)\
  (diff L)/(diff w_1) & = (hat(y) - y) dot f'(z) dot x_1\
$

Derivation for gradient of biases:
$
  L & = 1/2 (hat(y) - y)^2 \
  (diff L)/(diff b) & = (hat(y) - y) dot diff/(diff b) (f(z) - y)\
  (diff L)/(diff b) & = (hat(y) - y) dot f'(z) dot diff/(diff b_1) (sum_i w_i dot x_i + b)\
  (diff L)/(diff b) & = (hat(y) - y) dot f'(z) \
$
