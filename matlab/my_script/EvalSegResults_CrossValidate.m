clear all; close all;

% change values here
is_server        = 1;
down_sample_rate = 8;

id         = 'comp6';
%trainset  = 'trainval_aug';
trainset   = 'train_aug';

%testset   = 'trainval_aug';
testset    = 'val';

model_name = 'vgg128_ms';   %'vgg128_noup' or 'vgg128_ms'


if is_server
    VOC_root_folder = '/rmt/data/pascal/VOCdevkit';
else
    VOC_root_folder = '~/dataset/PASCAL/VOCdevkit';
end


best_avacc = -1;
best_w = -1;
best_x_std = -1;
best_r_std = -1;

for w = [1 5 10 15 20]
  for x_std = [10 20 30 40 50]
    for r_std = [10 20 30 40 50]
   
      post_folder = sprintf('post_densecrf_W%d_XStd%d_RStd%d_downsampleBy%d', w, x_std, r_std, down_sample_rate);

      save_root_folder = fullfile('/rmt/work/deeplabel/exper/voc12/res', model_name, testset, 'fc8', post_folder);

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% You do not need to chage values below
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
      seg_res_dir = [save_root_folder '/results/VOC2012/'];
      save_result_folder = fullfile(seg_res_dir, 'Segmentation', [id '_' testset '_cls']);

      seg_root = fullfile(VOC_root_folder, 'VOC2012');

      if ~exist(save_result_folder, 'dir')
        mkdir(save_result_folder);
      end

      VOCopts = GetVOCopts(seg_root, seg_res_dir, trainset, testset);

      % get iou score
      [accuracies, avacc, conf, rawcounts] = MyVOCevalseg(VOCopts, id);

      if best_avacc < avacc
        best_avacc = avacc;
        best_accuracies = accuracies;
        best_conf = conf;
        best_rawcounts = rawcounts;
        best_w = w;
        best_x_std = x_std;
        best_r_std = r_std;
      end
    end
  end
end

fprintf(1, 'Best avacc %6.3f%% occurs at w = %2.2f, x_std = %2.2f, r_std = %2.2f\n', best_avacc, best_w, best_x_std, best_r_std);
    

